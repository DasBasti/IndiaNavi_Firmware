//! The guarded formatting helper.
//!
//! Replaces: the `save_sprintf`, `save_snprintf` and `save_vsnprintf` macros
//! in `include/tasks.h:68-85`, and the hand-written copy of the same pattern
//! at `src/esp32/gui.c:156-159`.
//!
//! # What the C macros did
//!
//! ```c
//! #define save_sprintf(dest, format, ...)                 \
//!     do {                                                \
//!         xSemaphoreTake(print_semaphore, portMAX_DELAY); \
//!         sprintf(dest, format, ##__VA_ARGS__);           \
//!         xSemaphoreGive(print_semaphore);                \
//!     } while (0);
//! ```
//!
//! Three of them, differing only in which `printf` they call. The `LINUX`
//! branch of the same header (`include/tasks.h:23-24`) drops the semaphore
//! entirely — and, incidentally, defines `save_sprintf` to call `sprintf` with
//! the `snprintf` argument list, so a host build of the C tree passes the
//! buffer size as the format string. That bug does not survive the port.
//!
//! # What this does instead
//!
//! Rust's `format!` is reentrant and allocates its own buffer, so it does not
//! need a lock to be *correct*. The lock is kept anyway, for the reason the C
//! code actually needed it: the call sites format into a buffer another task
//! can be reading. `src/esp32/gui.c:157` writes `clock_label->text` from the
//! GPS task while the GUI task renders it; `src/screens/map_screen.c:107`
//! writes `gps_indicator_label->text` from a render callback. Keeping the
//! section means those call sites port across without having to re-derive the
//! argument each time.
//!
//! [`guarded_format`] replaces `save_sprintf`. [`guarded_format_truncated`]
//! replaces `save_snprintf`. `save_vsnprintf` needs no separate function:
//! `core::fmt::Arguments` *is* the pre-packaged argument list that `va_list`
//! was, so `save_vsnprintf(dest, size, format, args)` and
//! `save_snprintf(dest, size, format, ...)` are the same call here.
//!
//! The one behaviour worth naming: `snprintf` truncates, and
//! [`guarded_format_truncated`] truncates too, because the C buffers are fixed
//! (`battery_indicator_t::label_text` is `char[5]`,
//! `lib/Platinenmacher/gui/battery_indicator.h:18`). It truncates to
//! `capacity` **bytes** and never mid-UTF-8-character, so the result is always
//! valid; `snprintf`'s `capacity` includes the NUL terminator, this one does
//! not, so pass `BATTERY_CHARGE_STRBUF - 1`.

use crate::runtime::sync::CriticalSection;
use std::fmt::Arguments;
use std::time::Duration;

/// Format under `print_semaphore`, blocking for the section.
///
/// Replaces `save_sprintf(dest, format, ...)`
/// (`include/tasks.h:68-73`), whose `xSemaphoreTake(..., portMAX_DELAY)` is
/// the blocking take here.
///
/// ```ignore
/// // C: save_sprintf(clock_label->text, "%02d:%02d", hour, minute);
/// let text = guarded_format(&sections.print, format_args!("{hour:02}:{minute:02}"));
/// ```
pub fn guarded_format(print: &CriticalSection, args: Arguments<'_>) -> String {
    let _guard = print.lock();
    std::fmt::format(args)
}

/// Format under `print_semaphore` into at most `capacity` bytes.
///
/// Replaces `save_snprintf(dest, size, format, ...)` and
/// `save_vsnprintf(dest, size, format, args)` (`include/tasks.h:74-85`).
///
/// `capacity` is the number of bytes available for *text*; unlike
/// `snprintf`'s `size` it does not have to leave room for a NUL. Truncation
/// happens on a character boundary, so the result is never a partial UTF-8
/// sequence — `snprintf` would have cut mid-sequence.
pub fn guarded_format_truncated(
    print: &CriticalSection,
    capacity: usize,
    args: Arguments<'_>,
) -> String {
    let mut formatted = guarded_format(print, args);
    truncate_to(&mut formatted, capacity);
    formatted
}

/// Format under `print_semaphore`, giving up if the section is busy.
///
/// Replaces the `if (xSemaphoreTake(print_semaphore, 1000))` call sites in
/// `src/screens/test_screen.c:64`, `:79`, `:120` and `:172`, which skip the
/// update rather than stall a render. `None` is their `else` branch: nothing
/// was written.
pub fn guarded_format_timeout(
    print: &CriticalSection,
    timeout: Duration,
    args: Arguments<'_>,
) -> Option<String> {
    let _guard = print.lock_timeout(timeout)?;
    Some(std::fmt::format(args))
}

/// Shorten `s` to at most `capacity` bytes without splitting a character.
fn truncate_to(s: &mut String, capacity: usize) {
    if s.len() <= capacity {
        return;
    }
    let mut end = capacity;
    while end > 0 && !s.is_char_boundary(end) {
        end -= 1;
    }
    s.truncate(end);
}

/// A [`CriticalSection`] bound to the formatting helpers.
///
/// Convenience for the ported call sites, which nearly all format against
/// `print_semaphore` and would otherwise repeat `&sections.print` on every
/// line. Holds a borrow, so it costs nothing and cannot outlive the section.
#[derive(Debug, Clone, Copy)]
pub struct GuardedFormatter<'a> {
    print: &'a CriticalSection,
}

impl<'a> GuardedFormatter<'a> {
    /// Bind to `print_semaphore`.
    pub fn new(print: &'a CriticalSection) -> Self {
        GuardedFormatter { print }
    }

    /// See [`guarded_format`].
    pub fn format(&self, args: Arguments<'_>) -> String {
        guarded_format(self.print, args)
    }

    /// See [`guarded_format_truncated`].
    pub fn format_truncated(&self, capacity: usize, args: Arguments<'_>) -> String {
        guarded_format_truncated(self.print, capacity, args)
    }

    /// See [`guarded_format_timeout`].
    pub fn format_timeout(&self, timeout: Duration, args: Arguments<'_>) -> Option<String> {
        guarded_format_timeout(self.print, timeout, args)
    }

    /// The section being held.
    pub fn section(&self) -> &'a CriticalSection {
        self.print
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn format_matches_the_c_clock_label_sprintf() {
        let print = CriticalSection::new("print_semaphore");
        // src/esp32/gui.c:157: sprintf(clock_label->text, "%02d:%02d", h, m)
        let text = guarded_format(&print, format_args!("{:02}:{:02}", 7, 5));
        assert_eq!(text, "07:05");
        assert!(!print.is_held(), "the section must be released again");
    }

    #[test]
    fn truncation_matches_snprintf_on_a_fixed_c_buffer() {
        let print = CriticalSection::new("print_semaphore");
        // battery_indicator_t::label_text is char[5], so four bytes of text.
        let text = guarded_format_truncated(&print, 4, format_args!("{}%", 100));
        assert_eq!(text, "100%");
        let text = guarded_format_truncated(&print, 4, format_args!("{}%", 1000));
        assert_eq!(text, "1000");
        // Shorter than the capacity is left alone.
        let text = guarded_format_truncated(&print, 4, format_args!("{}%", 7));
        assert_eq!(text, "7%");
    }

    #[test]
    fn truncation_never_splits_a_character() {
        let print = CriticalSection::new("print_semaphore");
        // "°" is two bytes; a capacity of 2 must drop it rather than halve it.
        let text = guarded_format_truncated(&print, 2, format_args!("1°C"));
        assert_eq!(text, "1");
        let text = guarded_format_truncated(&print, 3, format_args!("1°C"));
        assert_eq!(text, "1°");
        let text = guarded_format_truncated(&print, 0, format_args!("1°C"));
        assert_eq!(text, "");
    }

    #[test]
    fn format_timeout_gives_up_while_the_section_is_busy() {
        let print = CriticalSection::new("print_semaphore");
        let _held = print.lock();
        assert_eq!(
            guarded_format_timeout(&print, Duration::from_millis(20), format_args!("x")),
            None
        );
    }

    #[test]
    fn the_helper_really_serialises_its_callers() {
        let print = Arc::new(CriticalSection::new("print_semaphore"));
        let mut threads = Vec::new();
        for i in 0..8u32 {
            let print = print.clone();
            threads.push(thread::spawn(move || {
                let mut last = String::new();
                for n in 0..100u32 {
                    last = guarded_format(&print, format_args!("{i}-{n}"));
                }
                last
            }));
        }
        let mut results: Vec<String> = threads.into_iter().map(|t| t.join().unwrap()).collect();
        results.sort();
        let expected: Vec<String> = (0..8u32).map(|i| format!("{i}-99")).collect();
        assert_eq!(results, expected);
        assert!(!print.is_held());
    }

    #[test]
    fn the_bound_formatter_is_the_same_helper() {
        let print = CriticalSection::new("print_semaphore");
        let f = GuardedFormatter::new(&print);
        assert_eq!(f.format(format_args!("{}", 42)), "42");
        assert_eq!(f.format_truncated(1, format_args!("{}", 42)), "4");
        assert_eq!(
            f.format_timeout(Duration::from_millis(10), format_args!("{}", 42)),
            Some("42".to_string())
        );
        assert_eq!(f.section().name(), "print_semaphore");
    }
}
