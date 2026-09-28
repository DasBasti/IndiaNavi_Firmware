//! Port of `lib/helper/readline.c` (declared in `lib/helper/helper.h`).
//!
//! Used by the firmware to walk the text files read off the SD card:
//! `src/esp32/wifi.c` (SSID on line 1, password on line 2), `src/esp32/gps.c`
//! (`TIMEZONE`), `src/esp32/map_loader.c` (waypoint/OTA files) and
//! `src/esp32/ota.c`.
//!
//! The C functions take a `char *source` cursor and return the cursor for the
//! next line, or `NULL` once the source is exhausted. The port keeps that
//! shape — `Option<&str>` in, `Option<&str>` out — so call sites translate
//! line for line, and so the quirks the firmware depends on stay observable:
//!
//! * `\r` is **skipped, not treated as a terminator**. `"a\rb"` is one line
//!   `"ab"`. Only `\n` ends a line.
//! * A source with no `\n` left yields the remaining text *and* a `None`
//!   cursor, so the last line of a file without a trailing newline is still
//!   returned — but the caller cannot distinguish "last line" from "empty" by
//!   the cursor alone; it must look at the destination, exactly as the C
//!   callers do.
//! * `readline(None, ..)` leaves the destination untouched (C returns before
//!   writing the terminator); `readline(Some(""), ..)` clears it.
//!
//! Where C wrote into a caller-supplied `char *destination` with no bounds
//! check, this port appends into a `&mut String`, which cannot overflow. That
//! is the one intentional deviation.

/// Read one line out of `source` into `destination`.
///
/// Returns the cursor for the next line, or `None` when `source` was `NULL`
/// (`None`) or held no further `\n`.
///
/// `destination` is cleared first, then filled with the line's bytes minus any
/// `\r`. Mirrors `char *readline(char *source, char *destination)`.
pub fn readline<'a>(source: Option<&'a str>, destination: &mut String) -> Option<&'a str> {
    // C: `if (source == 0) return 0;` — destination is not touched.
    let source = source?;

    destination.clear();

    let bytes = source.as_bytes();
    // Start of the current run of bytes to copy. `\r` and `\n` are ASCII, so
    // slicing at those offsets never splits a UTF-8 sequence.
    let mut start = 0;
    for (i, &b) in bytes.iter().enumerate() {
        match b {
            b'\n' => {
                destination.push_str(&source[start..i]);
                // C: `break` then `return ++source` — the cursor points just
                // past the newline, which may be the empty remainder.
                return Some(&source[i + 1..]);
            }
            b'\r' => {
                destination.push_str(&source[start..i]);
                start = i + 1;
            }
            _ => {}
        }
    }

    // C: `if (*source == 0) { *destination = 0; return 0; }` — the trailing
    // partial line is still handed to the caller, the cursor is NULL.
    destination.push_str(&source[start..]);
    None
}

/// Length of the first line of `source`, *including* room for the terminating
/// NUL, or `0` when there is no `\n` in `source`.
///
/// Mirrors `size_t countline(char *source)`, quirks included:
///
/// * `\r` bytes are not counted.
/// * A source without a `\n` returns `0`, **not** the length of the text. The
///   C caller `src/esp32/ota.c:101` feeds the result straight to
///   `RTOS_Malloc()`, so a URL file without a trailing newline allocates
///   nothing there. Preserved on purpose; fixing it is a firmware change, not
///   a porting change.
///
/// The count is in bytes, as in C, because the C callers use it as an
/// allocation size.
pub fn countline(source: Option<&str>) -> usize {
    let source = match source {
        Some(s) => s,
        None => return 0,
    };

    let mut count = 0;
    for &b in source.as_bytes() {
        if b == b'\n' {
            // C: `break` then `return ++count;`
            return count + 1;
        }
        if b == b'\r' {
            continue;
        }
        count += 1;
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    // The four tests below are ported from
    // `test/host/Platinenmacher/test_helper/test.c`, which is identical to
    // `test/embedded/test_helper/test_sd_readline.c`.

    #[test]
    fn null_is_null() {
        let mut dst = String::from("untouched");
        let next = readline(None, &mut dst);
        assert_eq!(next, None);
        // C returns before writing to destination.
        assert_eq!(dst, "untouched");
    }

    #[test]
    fn empty_string_is_empty() {
        let mut dst = String::from("\u{1}");
        let next = readline(Some(""), &mut dst);
        assert_eq!(dst, "");
        assert_eq!(next, None);
    }

    #[test]
    fn ignore_carriage_return() {
        let mut dst = String::from("\u{1}");
        let next = readline(Some("line one\rline two"), &mut dst);
        assert_eq!(dst, "line oneline two");
        assert_eq!(next, None);
    }

    #[test]
    fn two_lines_string_is_two_lines() {
        let mut dst = String::from("\u{1}");
        let next = readline(Some("line one\r\nline two"), &mut dst);
        assert_eq!(dst, "line one");

        let next = readline(next, &mut dst);
        assert_eq!(dst, "line two");
        assert_eq!(next, None);

        let next = readline(next, &mut dst);
        assert_eq!(next, None);
    }

    #[test]
    fn trailing_newline_yields_empty_remainder_then_ends() {
        // `"ssid\npass\n"` is what a WIFI file written by an editor looks
        // like; wifi.c reads exactly two lines out of it.
        let mut dst = String::new();
        let next = readline(Some("ssid\npass\n"), &mut dst);
        assert_eq!(dst, "ssid");

        let next = readline(next, &mut dst);
        assert_eq!(dst, "pass");
        // The cursor is the empty remainder after the final newline, not None:
        // C returns `++source`, a pointer to the NUL terminator.
        assert_eq!(next, Some(""));

        let next = readline(next, &mut dst);
        assert_eq!(dst, "");
        assert_eq!(next, None);
    }

    #[test]
    fn blank_line_in_the_middle_is_an_empty_line() {
        let mut dst = String::new();
        let next = readline(Some("a\n\nb"), &mut dst);
        assert_eq!(dst, "a");

        let next = readline(next, &mut dst);
        assert_eq!(dst, "");

        let next = readline(next, &mut dst);
        assert_eq!(dst, "b");
        assert_eq!(next, None);
    }

    #[test]
    fn non_ascii_bytes_survive_intact() {
        // C is byte-oriented; slicing on \r / \n must not split UTF-8.
        let mut dst = String::new();
        let next = readline(Some("Grünstraße\r\nnext"), &mut dst);
        assert_eq!(dst, "Grünstraße");
        assert_eq!(next, Some("next"));
    }

    #[test]
    fn countline_counts_line_plus_terminator() {
        assert_eq!(countline(Some("line one\r\nline two")), 9);
        assert_eq!(countline(Some("\n")), 1);
    }

    #[test]
    fn countline_without_newline_is_zero() {
        // Faithful to C: no '\n' means 0, not the text length.
        assert_eq!(countline(Some("no newline here")), 0);
        assert_eq!(countline(Some("")), 0);
        assert_eq!(countline(None), 0);
    }
}
