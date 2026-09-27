//! Display target for host tests.
//!
//! Replaces `test/host/Platinenmacher/mock/mock_display.h`. Available to
//! dependent crates through the `mock` feature:
//!
//! ```toml
//! [dev-dependencies]
//! pm-core = { path = "../pm-core", features = ["mock"] }
//! ```

use crate::colors::Color;
use crate::display::{Display, DisplayTarget, FrameBuffer, Rotation};
use crate::error::{Error, Result};
use crate::geometric::Rect;

/// `DISPLAY_WIDTH` of the mock
pub const DISPLAY_WIDTH: u16 = 20;
/// `DISPLAY_HEIGHT` of the mock
pub const DISPLAY_HEIGHT: u16 = 20;
/// The mock keeps one byte per pixel, as the C mock did.
pub const DISPLAY_BPP: u8 = 8;

/// A display target that stores one `color_t` byte per pixel, like the C mock's
/// `write_pixel()`: `dsp->fb[(y * dsp->size.width) + x] = color`.
#[derive(Debug, Clone, Default)]
pub struct MockDisplay {
    /// Set to `false` to make [`DisplayTarget::write_pixel()`] report
    /// [`Error::Fail`]. This stands in for the C test setting
    /// `dsp->write_pixel = 0`, which made `display_pixel_draw()` fail.
    pub write_pixel_enabled: bool,
    /// How often [`DisplayTarget::update()`] was called.
    pub updates: usize,
}

impl MockDisplay {
    pub const fn new() -> Self {
        Self {
            write_pixel_enabled: true,
            updates: 0,
        }
    }

    /// A [`Display`] of the mock's size, 8 bpp and unrotated, which is what
    /// `setUp()` of the C tests built.
    pub fn display() -> Display<MockDisplay> {
        Display::new(
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_BPP,
            Rotation::Deg0,
            MockDisplay::new(),
        )
    }
}

impl DisplayTarget for MockDisplay {
    fn write_pixel(&mut self, fb: FrameBuffer<'_>, x: i16, y: i16, color: Color) -> Result<()> {
        if !self.write_pixel_enabled {
            return Err(Error::Fail);
        }
        let index = y as usize * fb.size.width as usize + x as usize;
        match fb.data.get_mut(index) {
            Some(pixel) => {
                *pixel = color.as_u8();
                Ok(())
            }
            None => Err(Error::OutOfBounds),
        }
    }

    /// The C mock's `decompress()`: `data[x * size->width + y]`. Bytes outside
    /// `data` and values that are no `color_t` read as [`Color::Black`], where
    /// the C mock read past the end of the array.
    fn decompress(&self, size: &Rect, x: i16, y: i16, data: &[u8]) -> Color {
        let index = x as usize * size.width as usize + y as usize;
        let raw = data.get(index).copied().unwrap_or(0);
        Color::from_u8(raw).unwrap_or(Color::Black)
    }

    fn update(&mut self, _fb: &[u8]) -> Result<()> {
        self.updates += 1;
        Ok(())
    }
}
