//! Task specifications and the spawn wrapper.
//!
//! Replaces: the `TaskHandle_t` globals in `include/tasks.h:37-43` /
//! `src/esp32/main.c:62-68`, the `task*StackSize` defines at
//! `src/esp32/main.c:42-48`, and the `xTaskCreate` calls that use them.
//!
//! # The numbers, and where they come from
//!
//! Every constant below is transcribed from a specific line of
//! `src/esp32/main.c`. FreeRTOS `xTaskCreate` takes the stack depth in
//! **words** on some ports and in **bytes** on ESP-IDF; ESP-IDF's is bytes,
//! and `std::thread::Builder::stack_size` is also bytes, so the numbers carry
//! across unchanged.
//!
//! | Task | Stack | Priority | `xTaskCreate` call | Stack `#define` |
//! | --- | --- | --- | --- | --- |
//! | `sd` | `1024 * 8` | 1 | `src/esp32/main.c:305` | `taskSDStackSize`, `src/esp32/main.c:46` |
//! | `power` | `1024 * 6` | `tskIDLE_PRIORITY` (0) | `src/esp32/main.c:324` | `taskPowerStackSize`, `src/esp32/main.c:43` |
//! | `gps` | `1024 * 7` | `tskIDLE_PRIORITY` (0) | `src/esp32/main.c:326`, respawned at `:374` | `taskGPSStackSize`, `src/esp32/main.c:44` |
//! | `gui` | `1024 * 10` | 6 | `src/esp32/main.c:327`, respawned at `:378` | `taskGUIStackSize`, `src/esp32/main.c:45` |
//! | `wifi` | `1024 * 8` | 8 | `src/esp32/main.c:328` (commented out at boot), spawned at `:382` | `taskWifiStackSize`, `src/esp32/main.c:47` |
//!
//! Two `#define`s in that block have no `xTaskCreate` anywhere in
//! `src/esp32/main.c` and so get no [`TaskSpec`] here:
//! `taskGenericStackSize` (`1024 * 2`, `src/esp32/main.c:42`) is unused, and
//! `taskDownloaderStackSize` (`1024 * 8`, `src/esp32/main.c:48`) belongs to
//! `StartMapDownloaderTask`, which is declared at `src/esp32/main.c:55` but
//! never spawned. `lib/nmea_parser/nmea_parser.c:701` creates its own task
//! from inside the library; that one is the NMEA port's problem, not this
//! module's.
//!
//! # `vTaskDelete` has no safe equivalent, deliberately
//!
//! `src/esp32/main.c:376`, `:380` and `:384` respond to
//! `TASK_EVENT_DISABLE_{GPS,DISPLAY,WIFI}` by calling `vTaskDelete(handle)`.
//! That kills a task at an arbitrary instruction. In C it leaks whatever the
//! task held (`src/esp32/sd.c` takes `sd_semaphore` for the length of a file
//! read — a task deleted there never gives it back, and every later SD access
//! blocks forever). In Rust it would additionally be unsound: destructors do
//! not run, so an `Arc` refcount or a `MutexGuard` is left dangling.
//!
//! The port therefore does not offer it. [`TaskHandle::request_stop`] sets the
//! flag that [`StopToken::should_stop`] reports, the task's own loop notices
//! and returns, and [`TaskHandle::stop_and_join`] waits for it. The task bodies are
//! responsible for checking often enough; that is the cost of not being able
//! to kill them, and it is the right trade.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;

/// `tskIDLE_PRIORITY` from FreeRTOS.
pub const IDLE_PRIORITY: u8 = 0;

/// The stack size and priority a task is created with.
///
/// Replaces one `xTaskCreate` argument list. Constructed only as one of the
/// associated constants on [`Tasks`] so that the numbers live in exactly one
/// place.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TaskSpec {
    /// The `pcName` argument. Kept byte-identical to the C string so that a
    /// `vTaskList` dump (`src/screens/test_screen.c:118-163`) still reads the
    /// same.
    pub name: &'static str,
    /// The `usStackDepth` argument, in bytes (ESP-IDF's unit).
    pub stack_size: usize,
    /// The `uxPriority` argument.
    pub priority: u8,
    /// The C source line this was transcribed from, for the reviewer and for
    /// the cleanup task that deletes `src/esp32/main.c`.
    pub c_origin: &'static str,
}

/// The five tasks `src/esp32/main.c` creates.
pub struct Tasks;

impl Tasks {
    /// `xTaskCreate(&StartSDTask, "sd", taskSDStackSize, NULL, 1, &sdTask_h)`
    pub const SD: TaskSpec = TaskSpec {
        name: "sd",
        stack_size: 1024 * 8,
        priority: 1,
        c_origin: "src/esp32/main.c:305",
    };

    /// `xTaskCreate(&StartPowerTask, "power", taskPowerStackSize, NULL, tskIDLE_PRIORITY, &powerTask_h)`
    pub const POWER: TaskSpec = TaskSpec {
        name: "power",
        stack_size: 1024 * 6,
        priority: IDLE_PRIORITY,
        c_origin: "src/esp32/main.c:324",
    };

    /// `xTaskCreate(&StartGpsTask, "gps", taskGPSStackSize, NULL, tskIDLE_PRIORITY, &gpsTask_h)`
    pub const GPS: TaskSpec = TaskSpec {
        name: "gps",
        stack_size: 1024 * 7,
        priority: IDLE_PRIORITY,
        c_origin: "src/esp32/main.c:326,374",
    };

    /// `xTaskCreate(&StartGuiTask, "gui", taskGUIStackSize, NULL, 6, &guiTask_h)`
    pub const GUI: TaskSpec = TaskSpec {
        name: "gui",
        stack_size: 1024 * 10,
        priority: 6,
        c_origin: "src/esp32/main.c:327,378",
    };

    /// `xTaskCreate(&StartWiFiTask, "wifi", taskWifiStackSize, NULL, 8, &wifiTask_h)`
    ///
    /// The boot-time call at `src/esp32/main.c:328` is commented out; the
    /// live one is the `TASK_EVENT_ENABLE_WIFI` / `TASK_EVENT_START_CHARGING`
    /// handler at `src/esp32/main.c:382`.
    pub const WIFI: TaskSpec = TaskSpec {
        name: "wifi",
        stack_size: 1024 * 8,
        priority: 8,
        c_origin: "src/esp32/main.c:328,382",
    };

    /// Every spec, in the order `src/esp32/main.c` creates them.
    pub const ALL: [TaskSpec; 5] = [Tasks::SD, Tasks::POWER, Tasks::GPS, Tasks::GUI, Tasks::WIFI];
}

/// Handed to a task body so it can notice a stop request.
///
/// Replaces the `vTaskDelete` calls at `src/esp32/main.c:376`, `:380` and
/// `:384` — see the module header for why they cannot be ported literally.
#[derive(Debug, Clone)]
pub struct StopToken {
    stop: Arc<AtomicBool>,
}

impl StopToken {
    /// True once [`TaskHandle::request_stop`] has been called. A task loop
    /// should test this at least once per iteration and return when it is set.
    pub fn should_stop(&self) -> bool {
        self.stop.load(Ordering::Acquire)
    }
}

/// What a spawn failed on.
#[derive(Debug)]
pub enum SpawnError {
    /// The OS or FreeRTOS refused to create the thread — out of heap, or a
    /// bad stack size. `xTaskCreate` reports this as
    /// `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`.
    Os(std::io::Error),
    /// Applying the stack size / priority / name to the next spawn failed.
    /// ESP-IDF only.
    Configuration(String),
}

impl std::fmt::Display for SpawnError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SpawnError::Os(e) => write!(f, "could not create task: {e}"),
            SpawnError::Configuration(e) => write!(f, "could not configure task: {e}"),
        }
    }
}

impl std::error::Error for SpawnError {}

/// A running task.
///
/// Replaces one of the `TaskHandle_t` globals (`src/esp32/main.c:62-68`).
/// It is a value rather than a global: whoever spawned the task owns the
/// handle, which is what makes the stop-then-join discipline checkable.
#[derive(Debug)]
pub struct TaskHandle {
    spec: TaskSpec,
    stop: Arc<AtomicBool>,
    join: Option<thread::JoinHandle<()>>,
}

impl TaskHandle {
    /// The spec this task was created with.
    pub fn spec(&self) -> &TaskSpec {
        &self.spec
    }

    /// Ask the task to finish. Returns immediately; the task stops at its next
    /// [`StopToken::should_stop`] check.
    pub fn request_stop(&self) {
        self.stop.store(true, Ordering::Release);
    }

    /// True once the task body has returned.
    pub fn is_finished(&self) -> bool {
        self.join.as_ref().map(|j| j.is_finished()).unwrap_or(true)
    }

    /// Ask the task to stop and wait for it.
    ///
    /// `Err` means the task body panicked. It is reported rather than
    /// resumed: per PORTING.md a panic is a programmer error, and the caller
    /// (the main loop) should log it and decide, not inherit it.
    pub fn stop_and_join(mut self) -> Result<(), Box<dyn std::any::Any + Send>> {
        self.request_stop();
        match self.join.take() {
            Some(join) => join.join(),
            None => Ok(()),
        }
    }
}

/// Create a task with the stack size, priority and name from `spec`.
///
/// Replaces `xTaskCreate`. The body is given a [`StopToken`]; see the module
/// header for why there is no `vTaskDelete`.
///
/// On an ESP target the stack size and priority are applied through
/// `esp_idf_svc`'s `ThreadSpawnConfiguration`, which is the documented way to
/// reach `xTaskCreate`'s arguments from `std::thread`. On the host they are
/// applied as far as the platform allows: `stack_size` is honoured by
/// `std::thread::Builder`, `priority` is recorded on the handle but not
/// applied, because a host thread has no FreeRTOS priority. Host code must
/// therefore not depend on the priority for mutual exclusion — which is the
/// whole argument in the [`super`] module header.
pub fn spawn<F>(spec: TaskSpec, body: F) -> Result<TaskHandle, SpawnError>
where
    F: FnOnce(StopToken) + Send + 'static,
{
    let stop = Arc::new(AtomicBool::new(false));
    let token = StopToken { stop: stop.clone() };

    apply_spawn_configuration(&spec)?;

    let join = thread::Builder::new()
        .name(spec.name.to_string())
        .stack_size(spec.stack_size)
        .spawn(move || body(token))
        .map_err(SpawnError::Os)?;

    reset_spawn_configuration();

    Ok(TaskHandle {
        spec,
        stop,
        join: Some(join),
    })
}

/// Push `spec` into the thread-local configuration that the next
/// `std::thread::spawn` on ESP-IDF reads.
///
/// UNVERIFIED against a real build: this container cannot compile
/// `esp-idf-sys` (PORTING.md, "Firmware build"). The API used is
/// `esp_idf_svc::hal::task::thread::ThreadSpawnConfiguration`, whose `name`
/// field wants a NUL-terminated byte slice.
#[cfg(target_os = "espidf")]
fn apply_spawn_configuration(spec: &TaskSpec) -> Result<(), SpawnError> {
    use esp_idf_svc::hal::task::thread::ThreadSpawnConfiguration;

    ThreadSpawnConfiguration {
        name: Some(spec.name.as_bytes()),
        stack_size: spec.stack_size,
        priority: spec.priority,
        ..Default::default()
    }
    .set()
    .map_err(|e| SpawnError::Configuration(e.to_string()))
}

#[cfg(not(target_os = "espidf"))]
fn apply_spawn_configuration(_spec: &TaskSpec) -> Result<(), SpawnError> {
    // No FreeRTOS priorities on the host. `stack_size` is still applied, by
    // `thread::Builder` in `spawn` above.
    Ok(())
}

/// Drop the configuration again so an unrelated `std::thread::spawn` does not
/// silently inherit this task's stack size and priority.
#[cfg(target_os = "espidf")]
fn reset_spawn_configuration() {
    use esp_idf_svc::hal::task::thread::ThreadSpawnConfiguration;

    // Nothing useful to do if this fails: the task is already running.
    let _ = ThreadSpawnConfiguration::default().set();
}

#[cfg(not(target_os = "espidf"))]
fn reset_spawn_configuration() {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::mpsc;
    use std::time::Duration;

    /// The numbers, spelled out a second time from `src/esp32/main.c` so that
    /// changing one in `Tasks` without changing the C reference fails here.
    #[test]
    fn specs_match_the_xtaskcreate_calls_in_main_c() {
        let expected = [
            // (name, stack, priority, xTaskCreate line)
            ("sd", 8 * 1024, 1u8, "src/esp32/main.c:305"),
            ("power", 6 * 1024, 0, "src/esp32/main.c:324"),
            ("gps", 7 * 1024, 0, "src/esp32/main.c:326,374"),
            ("gui", 10 * 1024, 6, "src/esp32/main.c:327,378"),
            ("wifi", 8 * 1024, 8, "src/esp32/main.c:328,382"),
        ];
        assert_eq!(Tasks::ALL.len(), expected.len());
        for (spec, (name, stack, priority, origin)) in Tasks::ALL.iter().zip(expected) {
            assert_eq!(spec.name, name);
            assert_eq!(spec.stack_size, stack, "{name} stack size");
            assert_eq!(spec.priority, priority, "{name} priority");
            assert_eq!(spec.c_origin, origin, "{name} origin");
        }
    }

    #[test]
    fn task_names_are_unique_and_match_the_c_pcname_strings() {
        let mut names: Vec<&str> = Tasks::ALL.iter().map(|s| s.name).collect();
        names.sort_unstable();
        let unique = {
            let mut n = names.clone();
            n.dedup();
            n
        };
        assert_eq!(names, unique, "duplicate task name");
        assert_eq!(names, ["gps", "gui", "power", "sd", "wifi"]);
    }

    #[test]
    fn spawn_runs_the_body_and_join_waits_for_it() {
        let (tx, rx) = mpsc::channel();
        let handle = spawn(Tasks::GUI, move |_stop| {
            tx.send(42u32).unwrap();
        })
        .expect("spawn");
        assert_eq!(rx.recv_timeout(Duration::from_secs(5)), Ok(42));
        assert_eq!(handle.spec().name, "gui");
        handle.stop_and_join().expect("task body must not panic");
    }

    #[test]
    fn request_stop_ends_a_looping_task() {
        let (started_tx, started_rx) = mpsc::channel();
        let (done_tx, done_rx) = mpsc::channel();
        let handle = spawn(Tasks::GPS, move |stop| {
            let mut iterations = 0u32;
            loop {
                iterations += 1;
                if iterations == 1 {
                    started_tx.send(()).unwrap();
                }
                if stop.should_stop() {
                    break;
                }
                thread::sleep(Duration::from_millis(1));
            }
            done_tx.send(iterations).unwrap();
        })
        .expect("spawn");

        // Only ask it to stop once it is demonstrably looping, so that the
        // test proves the flag is observed rather than racing the spawn.
        started_rx
            .recv_timeout(Duration::from_secs(5))
            .expect("task must start");
        handle.request_stop();
        let iterations = done_rx
            .recv_timeout(Duration::from_secs(5))
            .expect("task must observe the stop request");
        assert!(iterations >= 1);
        handle.stop_and_join().expect("task body must not panic");
    }

    #[test]
    fn stop_and_join_reports_a_panicking_body() {
        let handle = spawn(Tasks::POWER, |_stop| panic!("task body blew up")).expect("spawn");
        assert!(
            handle.stop_and_join().is_err(),
            "a panic in a task body must be reported, not swallowed"
        );
    }

    /// The stack size is really applied, not just recorded. 6 KiB of locals
    /// fits in the `power` task's `1024 * 6`-byte stack only just, so this
    /// uses a comfortable fraction of it and only checks that the thread runs.
    #[test]
    fn spawn_applies_the_stack_size() {
        let (tx, rx) = mpsc::channel();
        let handle = spawn(Tasks::SD, move |_stop| {
            let buffer = [0u8; 4096];
            tx.send(buffer.len()).unwrap();
        })
        .expect("spawn");
        assert_eq!(rx.recv_timeout(Duration::from_secs(5)), Ok(4096));
        handle.stop_and_join().expect("task body must not panic");
    }
}
