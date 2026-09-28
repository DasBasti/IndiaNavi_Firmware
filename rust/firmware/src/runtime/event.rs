//! The typed inter-task event channel.
//!
//! Replaces: `task_events_e` and `eventQueueHandle` from `include/tasks.h`
//! (lines 35 and 45-58), created at `src/esp32/main.c:278` as
//! `xQueueCreate(6, sizeof(uint32_t))`, drained by the main loop at
//! `src/esp32/main.c:357-387`, and fed from
//! `src/esp32/main.c:134`/`140` (power task) and `src/esp32/main.c:191`
//! (`xQueueSendFromISR`, button ISR).
//!
//! # Shape
//!
//! The C queue carried a bare `uint32_t` that was sometimes a `task_events_e`
//! and sometimes a GPIO number (`src/esp32/main.c:182-183` posts `I2C_INT`
//! down the same queue, and the receiver at `src/esp32/main.c:367` compares
//! against it). That overloading is what forced `TASK_EVENT_ENTER_LOW_POWER`
//! to start at 50 — high enough not to collide with a GPIO number. The port
//! carries an [`Event`] instead, so the collision cannot happen; the
//! accelerometer interrupt gets its own variant when `src/esp32/main.c`'s
//! `WITH_ACC` block is ported.
//!
//! The discriminants are still pinned to the C values with `#[repr(u32)]`, on
//! the same grounds as `pm_core::Error` (see PORTING.md "Errors"): while the C
//! tree still exists, a half-ported firmware may need to move one of these
//! across FFI. [`Event::from_raw`] and the `as u32` cast are the conversion,
//! and a unit test asserts the pinning.
//!
//! `TASK_EVENT_NO_EVENT = 0` has no variant, for the same reason `PM_OK` has
//! none: "no event" is the *absence* of a message, which in this API is
//! `Err(`[`RecvTimeoutError::Timeout`]`)` or `Ok(None)` from
//! [`EventReceiver::try_recv`]. `Event::from_raw(0)` therefore returns `None`.
//! The only place C constructed a `TASK_EVENT_NO_EVENT` value is
//! `src/esp32/main.c:125`, where it is a local initialised-but-unsent
//! placeholder.

use std::sync::mpsc::{self, RecvTimeoutError as MpscRecvTimeoutError, TrySendError};
use std::time::Duration;

/// Depth of the event queue.
///
/// `src/esp32/main.c:278`: `xQueueCreate(6, sizeof(uint32_t))`.
pub const QUEUE_DEPTH: usize = 6;

/// An event posted between tasks.
///
/// Replaces `task_events_e` (`include/tasks.h:45-58`). Discriminants are the
/// C values; do not reorder, append.
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Event {
    /// `TASK_EVENT_ENTER_LOW_POWER` (50)
    EnterLowPower = 50,
    /// `TASK_EVENT_ENABLE_GPS` (51)
    EnableGps = 51,
    /// `TASK_EVENT_DISABLE_GPS` (52)
    DisableGps = 52,
    /// `TASK_EVENT_ENABLE_DISPLAY` (53)
    EnableDisplay = 53,
    /// `TASK_EVENT_DISABLE_DISPLAY` (54)
    DisableDisplay = 54,
    /// `TASK_EVENT_ENABLE_WIFI` (55)
    EnableWifi = 55,
    /// `TASK_EVENT_DISABLE_WIFI` (56)
    DisableWifi = 56,
    /// `TASK_EVENT_BUTTON_DOWN` (57)
    ButtonDown = 57,
    /// `TASK_EVENT_BUTTON_UP` (58)
    ButtonUp = 58,
    /// `TASK_EVENT_START_CHARGING` (59)
    StartCharging = 59,
    /// `TASK_EVENT_STOP_CHARGING` (60)
    StopCharging = 60,
}

impl Event {
    /// Every variant, in C declaration order. Used by the pinning test and
    /// handy for exhaustiveness checks in later ports.
    pub const ALL: [Event; 11] = [
        Event::EnterLowPower,
        Event::EnableGps,
        Event::DisableGps,
        Event::EnableDisplay,
        Event::DisableDisplay,
        Event::EnableWifi,
        Event::DisableWifi,
        Event::ButtonDown,
        Event::ButtonUp,
        Event::StartCharging,
        Event::StopCharging,
    ];

    /// The C `task_events_e` value.
    pub const fn as_raw(self) -> u32 {
        self as u32
    }

    /// Recover an [`Event`] from a C `task_events_e` value.
    ///
    /// Returns `None` for `TASK_EVENT_NO_EVENT` (0) and for anything that is
    /// not a declared variant — notably the GPIO numbers the C queue also
    /// carried. Callers must not panic on `None`; per PORTING.md an
    /// unrecognised value is bad input, not a programmer error.
    pub const fn from_raw(raw: u32) -> Option<Event> {
        match raw {
            50 => Some(Event::EnterLowPower),
            51 => Some(Event::EnableGps),
            52 => Some(Event::DisableGps),
            53 => Some(Event::EnableDisplay),
            54 => Some(Event::DisableDisplay),
            55 => Some(Event::EnableWifi),
            56 => Some(Event::DisableWifi),
            57 => Some(Event::ButtonDown),
            58 => Some(Event::ButtonUp),
            59 => Some(Event::StartCharging),
            60 => Some(Event::StopCharging),
            _ => None,
        }
    }
}

/// Why a [`EventSender::send`] did not deliver.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SendError {
    /// The queue already holds [`QUEUE_DEPTH`] events. The event was dropped.
    ///
    /// This is what `xQueueSend(handle, &evt, 0)` does when the queue is full:
    /// it returns `errQUEUE_FULL` and the caller at `src/esp32/main.c:134`,
    /// `:140` and `:191` ignores it. The port surfaces it so a caller *can*
    /// react, but dropping remains the correct default — a full queue means
    /// the consumer is behind, and blocking a producer (especially the power
    /// task) behind it is worse than losing one event.
    Full,
    /// The [`EventReceiver`] was dropped. The queue no longer exists.
    ///
    /// FreeRTOS has no equivalent: the C queue outlives every task. In the
    /// port this means the main loop has exited, i.e. the firmware is on its
    /// way down.
    Disconnected,
}

/// Why a blocking [`EventReceiver::recv`] returned nothing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecvError;

/// Why a [`EventReceiver::recv_timeout`] returned nothing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecvTimeoutError {
    /// No event arrived within the timeout.
    ///
    /// The `pdFALSE` return of
    /// `xQueueReceive(eventQueueHandle, &event_num, ledDelay / portTICK_PERIOD_MS)`
    /// at `src/esp32/main.c:357`.
    Timeout,
    /// Every [`EventSender`] was dropped and the queue is empty.
    Disconnected,
}

/// Producer end of the event queue. Cloneable; every task that posts events
/// gets one.
#[derive(Debug, Clone)]
pub struct EventSender {
    tx: mpsc::SyncSender<Event>,
}

/// Consumer end of the event queue. Not cloneable — there is exactly one
/// consumer, the main loop at `src/esp32/main.c:357`.
#[derive(Debug)]
pub struct EventReceiver {
    rx: mpsc::Receiver<Event>,
}

/// Create the event queue.
///
/// Replaces `eventQueueHandle = xQueueCreate(6, sizeof(uint32_t))`
/// (`src/esp32/main.c:278`). Bounded at [`QUEUE_DEPTH`] with a non-blocking
/// send, which is the exact behaviour of the C `xQueueSend(..., 0)` calls: an
/// unbounded channel would silently grow without limit if the consumer
/// stalled, on a device with ~300 KB of heap.
pub fn channel() -> (EventSender, EventReceiver) {
    let (tx, rx) = mpsc::sync_channel(QUEUE_DEPTH);
    (EventSender { tx }, EventReceiver { rx })
}

impl EventSender {
    /// Post an event without blocking.
    ///
    /// Replaces `xQueueSend(eventQueueHandle, &evt, 0)`.
    ///
    /// # ISR callers
    ///
    /// `src/esp32/main.c:191` posts from the button ISR with
    /// `xQueueSendFromISR`. This method is **not** an
    /// `xQueueSendFromISR` replacement and must not be called from an
    /// `IRAM_ATTR` handler: it takes an internal lock and can therefore block,
    /// which is forbidden in ISR context. The ported button handler should
    /// instead use the esp-idf-svc GPIO subscription's task notification and
    /// post the [`Event`] from the resulting task. Keeping that split explicit
    /// is the point — in C the two paths shared one queue and one `uint32_t`,
    /// and only the `FromISR` suffix distinguished them.
    pub fn send(&self, event: Event) -> Result<(), SendError> {
        match self.tx.try_send(event) {
            Ok(()) => Ok(()),
            Err(TrySendError::Full(_)) => Err(SendError::Full),
            Err(TrySendError::Disconnected(_)) => Err(SendError::Disconnected),
        }
    }
}

impl EventReceiver {
    /// Take the next event, blocking indefinitely.
    pub fn recv(&self) -> Result<Event, RecvError> {
        self.rx.recv().map_err(|_| RecvError)
    }

    /// Take the next event, blocking for at most `timeout`.
    ///
    /// Replaces
    /// `xQueueReceive(eventQueueHandle, &event_num, ledDelay / portTICK_PERIOD_MS)`
    /// (`src/esp32/main.c:357`), whose `pdFALSE` return is
    /// [`RecvTimeoutError::Timeout`] here.
    pub fn recv_timeout(&self, timeout: Duration) -> Result<Event, RecvTimeoutError> {
        self.rx.recv_timeout(timeout).map_err(|e| match e {
            MpscRecvTimeoutError::Timeout => RecvTimeoutError::Timeout,
            MpscRecvTimeoutError::Disconnected => RecvTimeoutError::Disconnected,
        })
    }

    /// Take the next event if one is already queued.
    ///
    /// `Ok(None)` is `TASK_EVENT_NO_EVENT`: nothing was waiting.
    pub fn try_recv(&self) -> Result<Option<Event>, RecvError> {
        match self.rx.try_recv() {
            Ok(event) => Ok(Some(event)),
            Err(mpsc::TryRecvError::Empty) => Ok(None),
            Err(mpsc::TryRecvError::Disconnected) => Err(RecvError),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    /// Every `task_events_e` value from `include/tasks.h:45-58` maps to
    /// exactly one variant and back.
    #[test]
    fn discriminants_match_task_events_e() {
        let expected: [(Event, u32); 11] = [
            (Event::EnterLowPower, 50),
            (Event::EnableGps, 51),
            (Event::DisableGps, 52),
            (Event::EnableDisplay, 53),
            (Event::DisableDisplay, 54),
            (Event::EnableWifi, 55),
            (Event::DisableWifi, 56),
            (Event::ButtonDown, 57),
            (Event::ButtonUp, 58),
            (Event::StartCharging, 59),
            (Event::StopCharging, 60),
        ];
        assert_eq!(expected.len(), Event::ALL.len());
        for (event, raw) in expected {
            assert_eq!(event.as_raw(), raw, "{event:?}");
            assert_eq!(Event::from_raw(raw), Some(event));
        }
        // ALL is in declaration order and complete.
        let from_all: Vec<u32> = Event::ALL.iter().map(|e| e.as_raw()).collect();
        let from_expected: Vec<u32> = expected.iter().map(|(_, r)| *r).collect();
        assert_eq!(from_all, from_expected);
    }

    /// `TASK_EVENT_NO_EVENT` is the absence of a message, not a variant.
    #[test]
    fn no_event_and_gpio_numbers_are_not_events() {
        assert_eq!(Event::from_raw(0), None);
        // The C queue also carried raw GPIO numbers (src/esp32/main.c:191).
        assert_eq!(Event::from_raw(1), None);
        assert_eq!(Event::from_raw(49), None);
        assert_eq!(Event::from_raw(61), None);
        assert_eq!(Event::from_raw(u32::MAX), None);
    }

    #[test]
    fn send_and_receive_preserves_order() {
        let (tx, rx) = channel();
        let sent = [
            Event::ButtonDown,
            Event::ButtonUp,
            Event::EnableGps,
            Event::DisableGps,
        ];
        for event in sent {
            tx.send(event).expect("queue has room for four events");
        }
        for event in sent {
            assert_eq!(rx.recv(), Ok(event));
        }
        assert_eq!(rx.try_recv(), Ok(None));
    }

    #[test]
    fn order_is_preserved_across_a_thread_boundary() {
        let (tx, rx) = channel();
        let producer = thread::spawn(move || {
            for event in Event::ALL {
                // Blocking-free send into a depth-6 queue would overflow, so
                // the producer paces itself the way a real task does: post,
                // and let the consumer drain.
                while tx.send(event) == Err(SendError::Full) {
                    thread::yield_now();
                }
            }
        });
        for expected in Event::ALL {
            assert_eq!(rx.recv(), Ok(expected));
        }
        producer.join().expect("producer thread");
    }

    #[test]
    fn multiple_senders_share_one_queue() {
        let (tx, rx) = channel();
        let tx2 = tx.clone();
        tx.send(Event::StartCharging).unwrap();
        tx2.send(Event::StopCharging).unwrap();
        assert_eq!(rx.recv(), Ok(Event::StartCharging));
        assert_eq!(rx.recv(), Ok(Event::StopCharging));
    }

    #[test]
    fn recv_timeout_reports_timeout_when_nothing_arrives() {
        let (_tx, rx) = channel();
        let started = std::time::Instant::now();
        assert_eq!(
            rx.recv_timeout(Duration::from_millis(50)),
            Err(RecvTimeoutError::Timeout)
        );
        assert!(
            started.elapsed() >= Duration::from_millis(50),
            "recv_timeout returned early: {:?}",
            started.elapsed()
        );
    }

    #[test]
    fn recv_timeout_returns_an_event_that_arrives_in_time() {
        let (tx, rx) = channel();
        let producer = thread::spawn(move || {
            thread::sleep(Duration::from_millis(10));
            tx.send(Event::EnterLowPower).unwrap();
        });
        assert_eq!(
            rx.recv_timeout(Duration::from_secs(5)),
            Ok(Event::EnterLowPower)
        );
        producer.join().expect("producer thread");
    }

    /// A full queue drops, it does not block. This is `xQueueSend(..., 0)`.
    #[test]
    fn send_on_a_full_queue_drops_rather_than_blocking() {
        let (tx, rx) = channel();
        for _ in 0..QUEUE_DEPTH {
            assert_eq!(tx.send(Event::ButtonDown), Ok(()));
        }
        assert_eq!(tx.send(Event::ButtonUp), Err(SendError::Full));
        // Draining one makes room again, and the dropped event is gone for
        // good -- it is not queued behind the others.
        assert_eq!(rx.recv(), Ok(Event::ButtonDown));
        assert_eq!(tx.send(Event::ButtonUp), Ok(()));
        for _ in 1..QUEUE_DEPTH {
            assert_eq!(rx.recv(), Ok(Event::ButtonDown));
        }
        assert_eq!(rx.recv(), Ok(Event::ButtonUp));
        assert_eq!(rx.try_recv(), Ok(None));
    }

    #[test]
    fn dropping_the_receiver_disconnects_the_sender() {
        let (tx, rx) = channel();
        drop(rx);
        assert_eq!(tx.send(Event::ButtonDown), Err(SendError::Disconnected));
    }

    #[test]
    fn dropping_every_sender_disconnects_the_receiver() {
        let (tx, rx) = channel();
        tx.send(Event::ButtonUp).unwrap();
        drop(tx);
        // Queued events are still delivered before the disconnect is reported.
        assert_eq!(rx.recv(), Ok(Event::ButtonUp));
        assert_eq!(rx.recv(), Err(RecvError));
        assert_eq!(
            rx.recv_timeout(Duration::from_millis(1)),
            Err(RecvTimeoutError::Disconnected)
        );
        assert_eq!(rx.try_recv(), Err(RecvError));
    }
}
