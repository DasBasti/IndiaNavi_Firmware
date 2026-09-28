//! Port of `lib/helper/printfb.c` — dump a framebuffer as decimal numbers.
//!
//! The C `printf_fb()` walks the buffer and `printf()`s it straight to the
//! console. This port only *formats*: it returns a `String`, or writes into
//! any [`core::fmt::Write`] sink.
//!
//! That split is deliberate. The firmware never prints without holding the
//! print semaphore behind the `save_sprintf` / `save_snprintf` macros in
//! `include/tasks.h`; those macros are FreeRTOS concurrency plumbing and stay
//! in the firmware crate. Keeping the formatting pure means the firmware wraps
//! the semaphore around a value this crate produced, and the host tests can
//! assert on the exact text without capturing stdout.
//!
//! Layout is byte-identical to the C output: one row per `width` index, each
//! value followed by a comma (so every line ends in a trailing comma) and the
//! row terminated by `\n`. Note that C indexes `fb[height * i + j]`, i.e. the
//! outer loop strides by `height` — kept as-is.

use core::fmt::Write;

use crate::error::Error;

/// Format `fb` the way `printf_fb()` prints it.
///
/// Returns [`Error::OutOfBounds`] when `fb` is smaller than `width * height`,
/// or when that product overflows. The C version reads past the end of the
/// buffer instead.
pub fn format_fb(fb: &[u8], width: usize, height: usize) -> Result<String, Error> {
    let mut out = String::new();
    write_fb(&mut out, fb, width, height)?;
    Ok(out)
}

/// [`format_fb`] into a caller-supplied sink.
///
/// Returns [`Error::OutOfBounds`] on a short buffer and [`Error::Fail`] if the
/// sink itself fails.
pub fn write_fb<W: Write>(
    out: &mut W,
    fb: &[u8],
    width: usize,
    height: usize,
) -> Result<(), Error> {
    let needed = width.checked_mul(height).ok_or(Error::OutOfBounds)?;
    if fb.len() < needed {
        return Err(Error::OutOfBounds);
    }

    for i in 0..width {
        for j in 0..height {
            write!(out, "{},", fb[height * i + j]).map_err(|_| Error::Fail)?;
        }
        out.write_char('\n').map_err(|_| Error::Fail)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rows_end_in_a_trailing_comma_and_newline() {
        let fb = [0u8, 1, 2, 3, 4, 5];
        // width = 2 rows of height = 3 values, as printf_fb() indexes them.
        assert_eq!(format_fb(&fb, 2, 3).unwrap(), "0,1,2,\n3,4,5,\n");
    }

    #[test]
    fn values_are_printed_as_unsigned_decimals() {
        let fb = [255u8, 0];
        assert_eq!(format_fb(&fb, 1, 2).unwrap(), "255,0,\n");
    }

    #[test]
    fn empty_geometry_produces_no_output() {
        assert_eq!(format_fb(&[], 0, 0).unwrap(), "");
        // width rows of zero values are still rows.
        assert_eq!(format_fb(&[], 3, 0).unwrap(), "\n\n\n");
    }

    #[test]
    fn short_buffer_is_rejected_instead_of_read_past_the_end() {
        let fb = [0u8; 5];
        assert_eq!(format_fb(&fb, 2, 3), Err(Error::OutOfBounds));
    }

    #[test]
    fn overflowing_geometry_is_rejected() {
        let fb = [0u8; 1];
        assert_eq!(format_fb(&fb, usize::MAX, 2), Err(Error::OutOfBounds));
    }

    #[test]
    fn write_fb_appends_to_the_sink() {
        let mut out = String::from("fb:\n");
        write_fb(&mut out, &[7u8, 8], 1, 2).unwrap();
        assert_eq!(out, "fb:\n7,8,\n");
    }
}
