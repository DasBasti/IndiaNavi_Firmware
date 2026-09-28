//! Text rendering, ported from `lib/Platinenmacher/display.c`.
//!
//! Covers `display_text_draw()`, its never-implemented sibling
//! `display_text_draw_len()` and the bitmap blitter they build on,
//! `display_draw_raw_rot()`.

use crate::{strlen, Font, Rotation};
use pm_core::{in_bounds, Color, Display, Error, Result};

/// Pixels the cursor advances per character.
///
/// C quirk: `display_text_draw()` places every glyph at `x + column * 8`
/// regardless of `font->width`, so wider fonts overlap and narrower ones leave
/// a gap. Kept as-is so placement matches the C renderer exactly.
const COLUMN_ADVANCE: i16 = 8;

/// Extra pixels between two rendered lines (`font->height + 2` in C).
const LINE_SPACING: i16 = 2;

/// Draws a `NUL` terminated string starting at `(x, y)`.
///
/// Port of `display_text_draw()`. Interprets `\n` as a line break, `\r` as a
/// carriage return and `\t` as a tab to the next multiple of eight columns;
/// every other byte is looked up in `font` and blitted.
///
/// Unlike the C version, which always returns `PM_OK` and ignores what
/// `display_pixel_draw()` told it, this returns [`Error::OutOfBounds`] if any
/// pixel of the text fell outside the framebuffer. Whatever did fit is still
/// drawn, so the sticky error only reports; it never aborts the run.
///
/// Characters the font has no glyph for are skipped (the C code reads past the
/// end of the table for those), but they still advance the cursor.
pub fn draw_text<D: Display + ?Sized>(
    dsp: &mut D,
    font: &Font,
    x: i16,
    y: i16,
    text: &[u8],
    color: Color,
) -> Result {
    draw_text_len(dsp, font, x, y, text, strlen(text), color)
}

/// Draws at most `len` bytes of `text` starting at `(x, y)`.
///
/// `display_text_draw_len()` is declared in `display.h` but was never
/// implemented; this is the length bounded counterpart of [`draw_text`], with
/// the colour argument the declaration was missing. `text` is not `NUL`
/// terminated here: exactly `min(len, text.len())` bytes are rendered.
pub fn draw_text_len<D: Display + ?Sized>(
    dsp: &mut D,
    font: &Font,
    x: i16,
    y: i16,
    text: &[u8],
    len: usize,
    color: Color,
) -> Result {
    let mut result = Ok(());
    let mut line: i16 = 0;
    let mut column: i16 = 0;

    for &c in &text[..len.min(text.len())] {
        match c {
            b'\n' => {
                line = line.wrapping_add(1);
                column = 0;
            }
            b'\r' => column = 0,
            // C quirk: `column += 8 - ((column + 1) % 8)` overshoots by one
            // compared to a plain "advance to the next multiple of 8".
            b'\t' => column = column.wrapping_add(COLUMN_ADVANCE - ((column + 1) % COLUMN_ADVANCE)),
            _ => {
                if let Some(glyph) = font.glyph(c) {
                    let gx = x.wrapping_add(column.wrapping_mul(COLUMN_ADVANCE));
                    let gy = y.wrapping_add(line.wrapping_mul(font.height() as i16 + LINE_SPACING));
                    let drawn = draw_raw_rot(
                        dsp,
                        glyph,
                        gx,
                        gy,
                        font.width(),
                        font.height(),
                        color,
                        None,
                        font.rotation(),
                    );
                    if result.is_ok() {
                        result = drawn;
                    }
                }
                column = column.wrapping_add(1);
            }
        }
    }

    result
}

/// Blits a packed monochrome bitmap.
///
/// Port of `display_draw_raw_rot()`. Bits are read LSB first, eight pixels per
/// byte, running down a column for [`Rotation::Rotate0`] and across a row for
/// [`Rotation::Rotate90`]; every other rotation is treated as `Rotate0`,
/// exactly as the C `if (rot == DISPLAY_ROTATE_90) ... else ...` does.
///
/// `background` is the colour for cleared bits; `None` is the C `TRANSPARENT`,
/// which `display_pixel_draw()` skips -- after its bounds check, so an off
/// screen transparent pixel still reports [`Error::OutOfBounds`].
// Mirrors the C `display_draw_raw_rot()` signature one for one; collapsing the
// arguments into a struct would make the two harder to diff.
#[allow(clippy::too_many_arguments)]
pub fn draw_raw_rot<D: Display + ?Sized>(
    dsp: &mut D,
    img: &[u8],
    x0: i16,
    y0: i16,
    width: u8,
    height: u8,
    foreground: Color,
    background: Option<Color>,
    rotation: Rotation,
) -> Result {
    let width = width as i32;
    let height = height as i32;
    if width == 0 {
        return Ok(());
    }

    let mut result = Ok(());
    let bytes = ((width * height) / 8) as usize;

    for (p, &field) in img.iter().enumerate().take(bytes) {
        for i in 0..8i32 {
            let color = if field & (1 << i) != 0 {
                Some(foreground)
            } else {
                background
            };

            let major = (p as i32 / width) * 8 + i;
            let minor = p as i32 % width;
            let (x, y) = match rotation {
                Rotation::Rotate90 => (x0 as i32 + major, y0 as i32 + minor),
                _ => (x0 as i32 + minor, y0 as i32 + major),
            };
            let (x, y) = (x as i16, y as i16);

            let step = match color {
                Some(color) => dsp.draw_pixel(x, y, color),
                // TRANSPARENT: nothing is written, but the bounds check in
                // `display_pixel_draw()` still runs before the early return.
                None if !in_bounds(dsp, x, y) => Err(Error::OutOfBounds),
                None => Ok(()),
            };
            if result.is_ok() {
                result = step;
            }
        }
    }

    result
}
