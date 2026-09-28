//! Concurrency scaffolding: tasks, the inter-task event channel, the critical
//! sections and the shared application state.
//!
//! Replaces: `include/tasks.h` (the `task_events_e` enum, `eventQueueHandle`,
//! the `*Task_h` `TaskHandle_t` globals, the `print_semaphore` /
//! `gui_semaphore` / `sd_semaphore` `SemaphoreHandle_t` globals and the
//! `save_sprintf` / `save_snprintf` / `save_vsnprintf` macros), together with
//! the definitions of those objects in `src/esp32/main.c:58-70` and the
//! mutable globals declared in `include/gui.h:35-50` (`clock_label`,
//! `north_indicator_label`, `wifi_indicator_label`, `gps_indicator_label`,
//! `sd_indicator_label`, `battery_indicator`, `wifi_indicator_image_data`,
//! `map_position`, `current_battery_level`, `is_charging`).
//!
//! # Why the C code was safe, and why that argument does not survive the port
//!
//! Most of the C globals above are written by one task and read by another
//! with no lock at all. `src/esp32/gps.c:153` publishes `map_position` from
//! the GPS task; `src/screens/map_screen.c:59-123` dereferences it from the
//! GUI task. `src/esp32/main.c:474-476` writes `current_battery_level` from
//! the power task and `src/esp32/main.c:354` reads it from the main task.
//! `src/esp32/wifi.c:219-227` swaps `wifi_indicator_image_data` from the WiFi
//! task while the GUI task renders from it.
//!
//! That code is not racing *often*, for three reasons that are all accidents
//! of FreeRTOS configuration rather than properties of the program:
//!
//! 1. **Priorities.** `gui` runs at 6 and `wifi` at 8, while `gps` and `power`
//!    run at `tskIDLE_PRIORITY`. A higher-priority task is not preempted by a
//!    lower-priority one, so a `gui`-task read is never interrupted by a
//!    `gps`-task write.
//! 2. **Word-sized writes.** `int32_t` and pointer stores are single
//!    instructions on Xtensa, so a torn value is not observable even when the
//!    two tasks do overlap.
//! 3. **Single core in practice.** Two tasks on two cores break both of the
//!    above immediately, and the sdkconfigs do enable both cores.
//!
//! None of that is something Rust will let us assume, and none of it is
//! something a reader can check locally. So the port states the discipline
//! explicitly, per item:
//!
//! | C object | Rust | Why |
//! | --- | --- | --- |
//! | `eventQueueHandle` | [`event`] channel | One-way, many producers, one consumer, and the consumer must be able to block with a timeout. That is a channel, not a lock: making it a `Mutex<VecDeque<_>>` would force the reader to poll. |
//! | `print_semaphore` | [`sync::CriticalSection`] | Guards a *device* (the shared format buffer / UART), not a value. Nothing is handed between tasks, so there is no message to send. |
//! | `gui_semaphore` | [`sync::CriticalSection`] | Same: it serialises the render pass against a second render request. |
//! | `sd_semaphore` | [`sync::CriticalSection`] + [`sync::ReadyGate`] | Serialises FATFS access *and* doubles as "the card is mounted" in the C code (`src/esp32/sd.c:254-256` creates it already taken; `src/esp32/main.c:306-308` and `src/esp32/sd.c:80-82` spin on the handle being non-NULL). Those are two different jobs and the port splits them. |
//! | `map_position`, `current_battery_level`, `is_charging` | [`state::AppState`] (one `Mutex`) | Read far more often than written, read *together* by the map screen, and always wanted "latest value", never "every value". A channel would make the reader responsible for keeping a local copy up to date; a lock does not. They share one `Mutex` rather than three because `map_screen.c` reads several fields in one render and a per-field lock would let it see a half-updated position. |
//! | the `label_t*` indicator globals, `battery_indicator`, `wifi_indicator_image_data` | [`state::Indicators`], inside the same `Mutex` | These are *widget pointers* in C, shared so that producer tasks can poke a consumer's UI objects. The port does not share widgets. The GUI task owns every widget; producers publish plain data here and the GUI task copies it into its own labels during the render pass. That removes the sharing rather than locking it. |
//!
//! The one rule that makes the table hold: **no `static mut`, and no global
//! mutable state.** Everything above is reached through an [`Arc`] handed to
//! each task at spawn time, so ownership is visible in the spawn call.
//!
//! [`Arc`]: std::sync::Arc

pub mod event;
pub mod fmt;
pub mod state;
pub mod sync;
pub mod task;

pub use event::{Event, EventReceiver, EventSender, RecvError, RecvTimeoutError, SendError};
pub use fmt::{guarded_format, guarded_format_timeout, guarded_format_truncated, GuardedFormatter};
pub use state::{
    AppState, BatteryState, GpsFix, Indicators, MapPosition, SdStatus, SharedState, WifiLevel,
};
pub use sync::{CriticalSection, CriticalSections, Guard, ReadyGate};
pub use task::{SpawnError, StopToken, TaskHandle, TaskSpec, Tasks, IDLE_PRIORITY};

use std::sync::Arc;

/// Everything a task needs from the runtime, in one value.
///
/// Replaces the block of globals at `src/esp32/main.c:58-70`. A task is handed
/// a clone of this at spawn time instead of reaching for a `static`; the
/// `Arc`s are what make "this task can touch the SD lock" visible at the call
/// site rather than implicit in a header.
#[derive(Clone)]
pub struct Context {
    /// `print_semaphore`, `gui_semaphore`, `sd_semaphore`.
    pub sections: Arc<CriticalSections>,
    /// The C globals from `include/gui.h`.
    pub state: Arc<SharedState>,
    /// The producer end of `eventQueueHandle`.
    pub events: EventSender,
}

impl Context {
    /// Format under `print_semaphore`. See [`fmt`].
    pub fn formatter(&self) -> GuardedFormatter<'_> {
        GuardedFormatter::new(&self.sections.print)
    }
}

impl std::fmt::Debug for Context {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Context")
            .field("sections", &self.sections)
            .field("state", &self.state)
            .finish_non_exhaustive()
    }
}

/// Build the runtime: the critical sections, the shared state and the event
/// queue.
///
/// Replaces the initialisation at `src/esp32/main.c:276-278`. The
/// [`EventReceiver`] is returned separately because there is exactly one
/// consumer — the main loop — and handing it out by value is what stops a
/// second task from quietly stealing events.
pub fn init() -> (Context, EventReceiver) {
    let (tx, rx) = event::channel();
    let context = Context {
        sections: Arc::new(CriticalSections::new()),
        state: Arc::new(SharedState::new()),
        events: tx,
    };
    (context, rx)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn init_wires_the_event_queue_to_the_returned_receiver() {
        let (context, events) = init();
        context.events.send(Event::ButtonDown).expect("queue empty");
        assert_eq!(events.recv(), Ok(Event::ButtonDown));
    }

    #[test]
    fn a_cloned_context_shares_one_state_and_one_set_of_sections() {
        let (context, _events) = init();
        let clone = context.clone();
        clone.state.set_gps_satellites(7);
        assert_eq!(context.state.snapshot().indicators.gps_satellites, 7);

        let guard = clone.sections.sd.lock();
        assert!(context.sections.sd.is_held());
        drop(guard);
        assert!(!context.sections.sd.is_held());
    }

    #[test]
    fn the_context_formatter_uses_the_print_section() {
        let (context, _events) = init();
        assert_eq!(
            context.formatter().format(std::format_args!("{:02}", 5)),
            "05"
        );
        assert_eq!(context.formatter().section().name(), "print_semaphore");
    }
}
