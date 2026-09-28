//! The framebuffer surface, ported from `lib/Platinenmacher/display.h`.
//!
//! Mirrors the part of `display_t` that drawing code actually uses: the
//! framebuffer extent and `display_pixel_draw()`. Drivers implement
//! [`Display`]; [`MockDisplay`] is the host side stand-in the C test suite
//! builds by hand in `test/host/Platinenmacher/test_display/test.c`.

use crate::error::{Error, Result};
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;

/// Colour index handed to the display driver, the C `uint8_t color`.
///
/// The concrete palette depends on the driver, so the numeric values live with
/// the driver rather than here; `colors.h` defines `BLACK` as `0`.
pub type Color = u8;

/// A framebuffer that pixels can be drawn into.
pub trait Display {
    /// Framebuffer width in pixels (`display_t::size.width`).
    fn width(&self) -> u16;

    /// Framebuffer height in pixels (`display_t::size.height`).
    fn height(&self) -> u16;

    /// Draws a single pixel.
    ///
    /// Port of `display_pixel_draw()`: implementations must return
    /// [`Error::OutOfBounds`] for coordinates outside the framebuffer and
    /// [`Error::Fail`] when no pixel writer is attached.
    fn draw_pixel(&mut self, x: i16, y: i16, color: Color) -> Result;
}

/// Whether `(x, y)` lies inside the framebuffer.
///
/// Same test `display_pixel_draw()` performs before touching the buffer.
pub fn in_bounds<D: Display + ?Sized>(dsp: &D, x: i16, y: i16) -> bool {
    x >= 0 && y >= 0 && (x as i32) < dsp.width() as i32 && (y as i32) < dsp.height() as i32
}

/// An in-memory [`Display`] for tests, one byte per pixel.
///
/// The equivalent of the `write_pixel()` callback plus `malloc()`ed
/// framebuffer that the C host tests set up in their `setUp()`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MockDisplay {
    width: u16,
    height: u16,
    pixels: Vec<Color>,
}

impl MockDisplay {
    /// Creates a `width` x `height` display cleared to colour `0` (`BLACK`).
    pub fn new(width: u16, height: u16) -> Self {
        MockDisplay {
            width,
            height,
            pixels: vec![0; width as usize * height as usize],
        }
    }

    /// Colour at `(x, y)`, or `None` if the coordinate is off screen.
    pub fn pixel(&self, x: i16, y: i16) -> Option<Color> {
        if !in_bounds(self, x, y) {
            return None;
        }
        Some(self.pixels[y as usize * self.width as usize + x as usize])
    }

    /// The whole framebuffer, row by row -- the C `display_t::fb`.
    pub fn framebuffer(&self) -> &[Color] {
        &self.pixels
    }

    /// Renders the framebuffer as ASCII art: `#` for any colour other than
    /// `0`, `.` otherwise, one line per row.
    ///
    /// Test expectations are written against this so a failing assertion shows
    /// the actual glyph rather than a wall of bytes.
    pub fn to_ascii(&self) -> String {
        let mut out = String::new();
        for y in 0..self.height as usize {
            if y > 0 {
                out.push('\n');
            }
            for x in 0..self.width as usize {
                out.push(if self.pixels[y * self.width as usize + x] != 0 {
                    '#'
                } else {
                    '.'
                });
            }
        }
        out
    }
}

impl Display for MockDisplay {
    fn width(&self) -> u16 {
        self.width
    }

    fn height(&self) -> u16 {
        self.height
    }

    fn draw_pixel(&mut self, x: i16, y: i16, color: Color) -> Result {
        if !in_bounds(self, x, y) {
            return Err(Error::OutOfBounds);
        }
        let idx = y as usize * self.width as usize + x as usize;
        self.pixels[idx] = color;
        Ok(())
    }
}
