//! `pm_core::display::DisplayTarget` impl for [`Acep5In65`].
//!
//! Replaces: the three function-pointer assignments at
//! lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.c:212-214, which
//! hung `ACEP_5IN65_Commit_Fb`, `ACEP_5IN65_Write` and
//! `ACEP_5IN65_Decompress_Pixel` off the `display_t` struct
//! (lib/Platinenmacher/display.h:35-39).
//!
//! # THIS MODULE IS NOT COMPILED AND NOT TESTED
//!
//! It is behind the off-by-default `pm-core` feature because `crates/pm-core`
//! does not exist on this branch: tsk_01M3HGZ51A2YCXJQQET91NVDT0 is marked
//! done but never pushed a PR, and the architect did not answer a request for
//! the trait definition. The signatures below are reconstructed from the C
//! `display_t` table and from that task's description ("Display ... delegates
//! pixel writes and flush to a DisplayTarget trait (replacing the
//! write_pixel/decompress/update function pointers)").
//!
//! Everything here is a thin forward to an inherent method on
//! [`Acep5In65`] that *is* compiled and tested -- [`Acep5In65::write_pixel`],
//! [`Acep5In65::update`] and [`decompress_pixel`]. So if the real trait turns
//! out to differ, the fix is confined to this file and does not touch the
//! panel logic.
//!
//! Note one shape change from C: `write_pixel` took a `const display_t *` only
//! so it could read `dsp->rotation` (C:30). The driver is constructed with its
//! own [`Rotation`], so the parameter is gone.

use pm_core::display::DisplayTarget;
use pm_core::geometry::Rect;

use embedded_hal::delay::DelayNs;
use embedded_hal::digital::{InputPin, OutputPin};
use embedded_hal::spi::SpiDevice;

use crate::driver::{decompress_pixel, Acep5In65, Error};

/// Maps the driver's errors onto the ported `error_code_t`
/// (lib/Platinenmacher/error.h). `TIMEOUT` and `OUT_OF_BOUNDS` are the two the
/// C driver itself produced; a dead SPI bus or GPIO had no C counterpart
/// (the C code asserted, C:104) and becomes `Unavailable`.
impl From<Error> for pm_core::Error {
    fn from(e: Error) -> Self {
        match e {
            Error::Timeout => pm_core::Error::Timeout,
            Error::OutOfBounds => pm_core::Error::OutOfBounds,
            Error::Spi(_) | Error::Pin(_) => pm_core::Error::Unavailable,
        }
    }
}

impl<SPI, DC, PWR, BUSY, DELAY> DisplayTarget for Acep5In65<SPI, DC, PWR, BUSY, DELAY>
where
    SPI: SpiDevice<u8>,
    DC: OutputPin,
    PWR: OutputPin,
    BUSY: InputPin,
    DELAY: DelayNs,
{
    /// C:212 `disp->write_pixel = ACEP_5IN65_Write`.
    fn write_pixel(&mut self, x: i16, y: i16, color: u8) -> pm_core::Result<()> {
        Acep5In65::write_pixel(self, x, y, color).map_err(pm_core::Error::from)
    }

    /// C:214 `disp->decompress = ACEP_5IN65_Decompress_Pixel`. Only
    /// `size->width` is read, exactly as in C:72.
    fn decompress(&self, size: &Rect, x: i16, y: i16, data: &[u8]) -> u8 {
        decompress_pixel(size.width, x, y, data)
    }

    /// C:212 `disp->update = ACEP_5IN65_Commit_Fb`.
    fn update(&mut self) -> pm_core::Result<()> {
        Acep5In65::update(self).map_err(pm_core::Error::from)
    }
}
