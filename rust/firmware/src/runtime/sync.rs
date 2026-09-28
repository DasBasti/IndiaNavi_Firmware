//! The three critical sections the C firmware protected with FreeRTOS mutexes.
//!
//! Replaces: `print_semaphore`, `gui_semaphore` and `sd_semaphore`
//! (`include/tasks.h:31-33`), created at `src/esp32/main.c:276-277` and
//! `src/esp32/sd.c:254`.
//!
//! # Why these are locks and not channels
//!
//! All three guard a *device*, not a value, and nothing is handed from one
//! task to another across them:
//!
//! - `print_semaphore` serialises formatting into a shared buffer and the
//!   newlib/vfs machinery behind it (`src/esp32/gui.c:156-159`,
//!   `src/screens/test_screen.c:64-177`, `src/screens/off_screen.c:118-120`).
//! - `gui_semaphore` serialises the render pass against a second render
//!   request (`src/esp32/gui.c:389-403`).
//! - `sd_semaphore` serialises FATFS access (`src/esp32/sd.c:100-148`,
//!   `src/esp32/gui_map_callbacks.c:52-154`, `src/screens/picture_screen.c:41-60`).
//!
//! A channel is the wrong shape for that: it would mean nominating one task as
//! the owner of the SD card and routing every read through it, which is a
//! bigger change than this task, and it would not help — the callers need the
//! *result* synchronously anyway.
//!
//! # Why not `std::sync::Mutex` directly
//!
//! Three C behaviours have no `std::sync::Mutex` equivalent:
//!
//! 1. **A timed take.** `xSemaphoreTake(sd_semaphore, pdTICKS_TO_MS(1000))`
//!    (`src/esp32/sd.c:100`, `src/esp32/gui_map_callbacks.c:52` and `:120`,
//!    `src/screens/picture_screen.c:41`, `src/screens/off_screen.c:124`) and
//!    `xSemaphoreTake(print_semaphore, 1000)`
//!    (`src/screens/test_screen.c:64`, `:79`, `:120`, `:172`) give up rather
//!    than block a render forever. `Mutex::lock` cannot time out.
//! 2. **A peek.** `uxSemaphoreGetCount(sd_semaphore)`
//!    (`src/esp32/gui_map_callbacks.c:35` and `:100`,
//!    `src/screens/test_screen.c:175`) tests whether the card is busy without
//!    taking it, so the map loader can skip a tile instead of stalling.
//! 3. **No poisoning.** A `std::sync::Mutex` stays poisoned forever after a
//!    holder panics. On a navigation device that turns one panic into a brick:
//!    every later SD access returns `Err` and the map never loads again.
//!    FreeRTOS has no such concept. [`CriticalSection`] follows FreeRTOS: the
//!    lock is released when the guard drops, panic or not.
//!
//! So this is a thin `Mutex<bool>` + `Condvar` with exactly those three
//! operations, and nothing else.
//!
//! # `sd_semaphore` did two jobs
//!
//! `src/esp32/sd.c:254-256` creates the mutex and immediately takes it, giving
//! it back only once the card is mounted (`:287`). Meanwhile
//! `src/esp32/main.c:306-308` and `src/esp32/sd.c:80-84` spin on the *handle*
//! being non-NULL. So the same object meant both "the card is mounted" and
//! "the card is not in use right now", and the NULL check meant "the SD task
//! has started". [`ReadyGate`] is the first of those; [`CriticalSection`] is
//! the second. Splitting them is the point: a caller that only needs to know
//! the card exists no longer has to take, and then give back, a lock.

use std::fmt;
use std::marker::PhantomData;
use std::sync::{Condvar, Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

/// One FreeRTOS mutex.
///
/// Replaces a `SemaphoreHandle_t` created with `xSemaphoreCreateMutex()`.
#[derive(Debug)]
pub struct CriticalSection {
    name: &'static str,
    held: Mutex<bool>,
    released: Condvar,
}

/// Proof that the caller holds a [`CriticalSection`]. Releases it on drop.
///
/// Deliberately `!Send`: a FreeRTOS mutex records its owner task and must be
/// given back by the task that took it, so a guard must not cross a task
/// boundary. `PhantomData<*const ()>` is what enforces that.
#[derive(Debug)]
pub struct Guard<'a> {
    section: &'a CriticalSection,
    _not_send: PhantomData<*const ()>,
}

impl CriticalSection {
    /// Create an unheld critical section. `name` appears in [`fmt::Debug`] and
    /// in the [`CriticalSections`] accessors; it is the C variable's name.
    pub const fn new(name: &'static str) -> Self {
        CriticalSection {
            name,
            held: Mutex::new(false),
            released: Condvar::new(),
        }
    }

    /// The C variable this stands in for.
    pub fn name(&self) -> &'static str {
        self.name
    }

    fn state(&self) -> MutexGuard<'_, bool> {
        // Never poison: see the module header. A panicking holder releases the
        // section (its `Guard` drops during the unwind) and the next taker is
        // entitled to it.
        self.held.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Take the section, blocking until it is free.
    ///
    /// Replaces `xSemaphoreTake(handle, portMAX_DELAY)`
    /// (`src/esp32/gui.c:156`, `src/esp32/sd.c:134` and `:145`,
    /// `src/screens/off_screen.c:118`).
    pub fn lock(&self) -> Guard<'_> {
        let mut held = self.state();
        while *held {
            held = self
                .released
                .wait(held)
                .unwrap_or_else(PoisonError::into_inner);
        }
        *held = true;
        Guard {
            section: self,
            _not_send: PhantomData,
        }
    }

    /// Take the section if it is free right now, otherwise give up.
    ///
    /// Replaces `xSemaphoreTake(handle, 0)` (`src/esp32/gui.c:389`).
    pub fn try_lock(&self) -> Option<Guard<'_>> {
        let mut held = self.state();
        if *held {
            return None;
        }
        *held = true;
        Some(Guard {
            section: self,
            _not_send: PhantomData,
        })
    }

    /// Take the section, giving up after `timeout`.
    ///
    /// Replaces `xSemaphoreTake(handle, pdTICKS_TO_MS(1000))` and friends.
    /// `None` is the `pdFALSE` return the C callers test for.
    pub fn lock_timeout(&self, timeout: Duration) -> Option<Guard<'_>> {
        let deadline = Instant::now() + timeout;
        let mut held = self.state();
        while *held {
            let remaining = match deadline.checked_duration_since(Instant::now()) {
                Some(remaining) if !remaining.is_zero() => remaining,
                _ => return None,
            };
            let (next, timed_out) = self
                .released
                .wait_timeout(held, remaining)
                .unwrap_or_else(PoisonError::into_inner);
            held = next;
            if timed_out.timed_out() && *held {
                return None;
            }
        }
        *held = true;
        Some(Guard {
            section: self,
            _not_send: PhantomData,
        })
    }

    /// True while some task holds the section.
    ///
    /// Replaces `!uxSemaphoreGetCount(handle)` — the C comment at
    /// `src/esp32/gui_map_callbacks.c:35` spells out that a free binary
    /// semaphore counts 1, so `!count` means "taken".
    ///
    /// This is a *hint*. It can be stale the instant it returns, exactly as in
    /// C; use it only to skip optional work, never to decide that an
    /// unsynchronised access is safe.
    pub fn is_held(&self) -> bool {
        *self.state()
    }
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        let mut held = self.section.state();
        *held = false;
        drop(held);
        // One waiter: the section admits one holder, so waking the rest would
        // only make them re-sleep. Matches `xSemaphoreGive`.
        self.section.released.notify_one();
    }
}

impl Guard<'_> {
    /// The section this guard holds.
    pub fn section(&self) -> &CriticalSection {
        self.section
    }
}

/// "Has the SD card finished mounting?"
///
/// Replaces the second job `sd_semaphore` was doing: `src/esp32/sd.c:254-256`
/// creates it already taken and gives it back at `:287` once
/// `esp_vfs_fat_sdspi_mount` has succeeded, while `src/esp32/main.c:306-308`
/// and `src/esp32/sd.c:80-84` busy-wait on the handle being non-NULL.
///
/// A gate, not a lock: it latches once and is never un-signalled, so a waiter
/// that arrives late does not block at all. The C spin loops become
/// [`ReadyGate::wait`].
#[derive(Debug)]
pub struct ReadyGate {
    name: &'static str,
    ready: Mutex<bool>,
    changed: Condvar,
}

impl ReadyGate {
    /// Create a gate that is not yet open.
    pub const fn new(name: &'static str) -> Self {
        ReadyGate {
            name,
            ready: Mutex::new(false),
            changed: Condvar::new(),
        }
    }

    /// What this gate is about, for logging.
    pub fn name(&self) -> &'static str {
        self.name
    }

    /// Open the gate and wake every waiter. Idempotent.
    ///
    /// Replaces the `xSemaphoreGive(sd_semaphore)` at `src/esp32/sd.c:287`
    /// that ends the mount.
    pub fn signal(&self) {
        let mut ready = self.ready.lock().unwrap_or_else(PoisonError::into_inner);
        *ready = true;
        drop(ready);
        self.changed.notify_all();
    }

    /// True once [`ReadyGate::signal`] has been called.
    ///
    /// Replaces the `if (!sd_semaphore)` NULL checks.
    pub fn is_ready(&self) -> bool {
        *self.ready.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Wait for the gate, giving up after `timeout`.
    ///
    /// `false` is the `PM_FAIL` that `waitForSDInit` returns at
    /// `src/esp32/sd.c:83` after 1000 ticks.
    pub fn wait(&self, timeout: Duration) -> bool {
        let deadline = Instant::now() + timeout;
        let mut ready = self.ready.lock().unwrap_or_else(PoisonError::into_inner);
        while !*ready {
            let remaining = match deadline.checked_duration_since(Instant::now()) {
                Some(remaining) if !remaining.is_zero() => remaining,
                _ => return false,
            };
            let (next, timed_out) = self
                .changed
                .wait_timeout(ready, remaining)
                .unwrap_or_else(PoisonError::into_inner);
            ready = next;
            if timed_out.timed_out() && !*ready {
                return false;
            }
        }
        true
    }
}

/// The three critical sections, in one place.
///
/// Replaces the `print_semaphore` / `gui_semaphore` / `sd_semaphore` globals.
/// Tasks receive an `Arc<CriticalSections>` at spawn time rather than reaching
/// for a `static`, so a reader can see from the spawn call which sections a
/// task can touch.
#[derive(Debug)]
pub struct CriticalSections {
    /// `print_semaphore` (`src/esp32/main.c:276`).
    pub print: CriticalSection,
    /// `gui_semaphore` (`src/esp32/main.c:277`).
    pub gui: CriticalSection,
    /// `sd_semaphore` (`src/esp32/sd.c:254`), the mutual-exclusion half.
    pub sd: CriticalSection,
    /// `sd_semaphore` (`src/esp32/sd.c:254-287`), the "card is mounted" half.
    pub sd_ready: ReadyGate,
}

impl CriticalSections {
    /// Create all four, all free and the gate shut.
    pub const fn new() -> Self {
        CriticalSections {
            print: CriticalSection::new("print_semaphore"),
            gui: CriticalSection::new("gui_semaphore"),
            sd: CriticalSection::new("sd_semaphore"),
            sd_ready: ReadyGate::new("sd_mounted"),
        }
    }
}

impl Default for CriticalSections {
    fn default() -> Self {
        CriticalSections::new()
    }
}

impl fmt::Display for CriticalSection {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} ({})",
            self.name,
            if self.is_held() { "held" } else { "free" }
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn a_free_section_can_be_taken_and_is_released_on_drop() {
        let cs = CriticalSection::new("print_semaphore");
        assert!(!cs.is_held());
        {
            let _guard = cs.lock();
            assert!(cs.is_held());
            assert!(cs.try_lock().is_none(), "already held");
        }
        assert!(!cs.is_held());
        assert!(cs.try_lock().is_some());
    }

    #[test]
    fn lock_timeout_gives_up_like_xsemaphoretake_with_a_tick_count() {
        let cs = CriticalSection::new("sd_semaphore");
        let _held = cs.lock();
        let started = Instant::now();
        assert!(cs.lock_timeout(Duration::from_millis(50)).is_none());
        assert!(
            started.elapsed() >= Duration::from_millis(50),
            "returned early: {:?}",
            started.elapsed()
        );
    }

    #[test]
    fn lock_timeout_succeeds_once_the_holder_releases() {
        let cs = Arc::new(CriticalSection::new("sd_semaphore"));
        let holder = {
            let cs = cs.clone();
            thread::spawn(move || {
                let _guard = cs.lock();
                thread::sleep(Duration::from_millis(20));
            })
        };
        while !cs.is_held() {
            thread::yield_now();
        }
        // Blocks until the holder above drops its guard, then succeeds.
        assert!(cs.lock_timeout(Duration::from_secs(5)).is_some());
        holder.join().unwrap();
    }

    /// The section really excludes: N threads incrementing a counter under it
    /// must not lose an update.
    #[test]
    fn the_section_provides_mutual_exclusion() {
        let cs = Arc::new(CriticalSection::new("gui_semaphore"));
        let counter = Arc::new(AtomicU32::new(0));
        let inside = Arc::new(AtomicU32::new(0));
        let mut threads = Vec::new();
        for _ in 0..8 {
            let cs = cs.clone();
            let counter = counter.clone();
            let inside = inside.clone();
            threads.push(thread::spawn(move || {
                for _ in 0..200 {
                    let _guard = cs.lock();
                    assert_eq!(
                        inside.fetch_add(1, Ordering::SeqCst),
                        0,
                        "two holders at once"
                    );
                    counter.fetch_add(1, Ordering::SeqCst);
                    inside.fetch_sub(1, Ordering::SeqCst);
                }
            }));
        }
        for t in threads {
            t.join().unwrap();
        }
        assert_eq!(counter.load(Ordering::SeqCst), 8 * 200);
    }

    /// FreeRTOS mutexes do not poison. Neither does this one: a panicking
    /// holder must not brick every later SD access.
    #[test]
    fn a_panicking_holder_does_not_poison_the_section() {
        let cs = Arc::new(CriticalSection::new("sd_semaphore"));
        let panicker = {
            let cs = cs.clone();
            thread::spawn(move || {
                let _guard = cs.lock();
                panic!("holder blew up");
            })
        };
        assert!(panicker.join().is_err());
        assert!(!cs.is_held(), "the guard must be released during unwind");
        assert!(
            cs.lock_timeout(Duration::from_millis(100)).is_some(),
            "the section must still be usable"
        );
    }

    #[test]
    fn ready_gate_latches_and_wakes_waiters() {
        let gate = Arc::new(ReadyGate::new("sd_mounted"));
        assert!(!gate.is_ready());
        assert!(
            !gate.wait(Duration::from_millis(20)),
            "must time out while shut"
        );

        let signaller = {
            let gate = gate.clone();
            thread::spawn(move || {
                thread::sleep(Duration::from_millis(10));
                gate.signal();
            })
        };
        assert!(gate.wait(Duration::from_secs(5)));
        signaller.join().unwrap();

        // Latched: a late waiter does not block, and signalling twice is fine.
        assert!(gate.is_ready());
        assert!(gate.wait(Duration::from_millis(0)));
        gate.signal();
        assert!(gate.is_ready());
    }

    #[test]
    fn critical_sections_bundle_names_the_c_globals() {
        let sections = CriticalSections::new();
        assert_eq!(sections.print.name(), "print_semaphore");
        assert_eq!(sections.gui.name(), "gui_semaphore");
        assert_eq!(sections.sd.name(), "sd_semaphore");
        assert_eq!(sections.sd_ready.name(), "sd_mounted");
        assert!(!sections.sd_ready.is_ready());
    }
}
