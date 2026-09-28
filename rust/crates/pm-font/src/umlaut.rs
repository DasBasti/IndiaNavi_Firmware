//! German umlaut folding, ported from `lib/helper/umlaut.c`.
//!
//! The firmware renders with ASCII-only font tables, so `gui/label.c` folds
//! the UTF-8 umlauts of a label into two ASCII characters immediately before
//! calling `display_text_draw()`. The replacement is the same length as the
//! sequence it replaces, so it happens in place.
//!
//! Callers keep that ordering: fold first, then draw. Nothing in
//! [`crate::text`] calls this for you, just as `display_text_draw()` does not.

/// Replaces UTF-8 umlauts in `text` with two-character ASCII equivalents.
///
/// Port of `convert_umlauts_inplace()`.
///
/// # Bug-for-bug
///
/// The C mapping is wrong and this port reproduces it verbatim rather than
/// silently changing what the display shows:
///
/// | input | C source | folds to | should be |
/// | --- | --- | --- | --- |
/// | `ä` | `C3 A4` | `ae` | `ae` |
/// | `ö` | `C3 B6` | `ae` | `oe` |
/// | `ü` | `C3 BC` | `ae` | `ue` |
/// | -- | `C3 2C` | `sz` | -- |
///
/// The last row is dead code: it is meant to catch `ß`, but `ß` is `C3 9F` in
/// UTF-8 and `C3 2C` is not a valid sequence, so the branch never fires.
///
/// The one deliberate difference from C: a trailing `C3` byte at the very end
/// of the buffer makes the C loop step over the `NUL` terminator and read on
/// into whatever follows. This version stops at the end of the slice.
pub fn convert_in_place(text: &mut [u8]) {
    let mut i = 0;
    while i < text.len() && text[i] != 0 {
        if text[i] == 0xc3 {
            i += 1;
            if i >= text.len() {
                break;
            }
            match text[i] {
                0xa4 => {
                    // ä
                    text[i - 1] = b'a';
                    text[i] = b'e';
                }
                0xb6 => {
                    // ö
                    text[i - 1] = b'a';
                    text[i] = b'e';
                }
                0xbc => {
                    // ü
                    text[i - 1] = b'a';
                    text[i] = b'e';
                }
                0x2c => {
                    // ß, never reached: see the note on this function
                    text[i - 1] = b's';
                    text[i] = b'z';
                }
                _ => {}
            }
        }
        i += 1;
    }
}
