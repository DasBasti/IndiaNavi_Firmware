//! Icon assets — replaces `lib/icons_32/icons_32.h` and `lib/icons_32/*.c`.
//!
//! The pixel data lives in [`icons_32`], which is generated from the C assets
//! by `rust/tools/gen-icons` and must not be edited by hand:
//!
//! ```text
//! cargo run --manifest-path rust/tools/gen-icons/Cargo.toml
//! ```
//!
//! This crate is deliberately standalone: it knows nothing about a display or
//! about `pm-gui`, which wraps [`Icon`] in its own image component.

#![no_std]

pub mod icons_32;

pub use icons_32::*;

/// Width and height of the 32px icons, from `#define ICON_SIZE` in
/// `lib/icons_32/icons_32.h`.
pub const ICON_SIZE: u16 = 32;

/// Bits per pixel of the icon data.
///
/// Each pixel is a 3-bit ACeP colour code and two pixels share a byte, high
/// nibble first — see `ACEP_5IN65_Decompress_Pixel` in
/// `lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.c`.
pub const BITS_PER_PIXEL: u32 = 4;

/// A static image asset: the Rust form of one `uint8_t <name>[]` from
/// `lib/icons_32/`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Icon {
    /// Width in pixels.
    pub width: u16,
    /// Height in pixels.
    pub height: u16,
    /// Packed pixel data, [`BITS_PER_PIXEL`] bits per pixel, row by row.
    pub data: &'static [u8],
}

impl Icon {
    /// Builds an icon from its dimensions and packed pixel data.
    pub const fn new(width: u16, height: u16, data: &'static [u8]) -> Self {
        Self {
            width,
            height,
            data,
        }
    }

    /// Number of bytes `width` x `height` pixels occupy at
    /// [`BITS_PER_PIXEL`] bits per pixel.
    pub const fn expected_data_len(&self) -> usize {
        self.width as usize * self.height as usize * BITS_PER_PIXEL as usize / 8
    }
}
