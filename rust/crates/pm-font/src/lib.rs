//! Bitmap fonts and text rendering for the Platinenmacher display stack.
//!
//! This is a port of
//!
//! * `lib/Platinenmacher/font.h` / `font.c` -- the [`Font`] model and the text
//!   measuring helpers,
//! * the text drawing part of `lib/Platinenmacher/display.c`
//!   (`display_text_draw()` / `display_draw_raw_rot()`) -- see [`text`],
//! * `lib/helper/umlaut.c` -- see [`umlaut`],
//! * all eight tables in `lib/Platinenmacher/fonts/` -- see [`fonts`].
//!
//! The crate is `no_std` and allocates nothing itself; the display surface it
//! draws onto comes from [`pm_core`], which is `no_std` + `alloc`.
//!
//! Glyph placement is bug-for-bug identical to the C implementation; every
//! place where the C code does something surprising is called out with a
//! `C quirk:` note so the port can be diffed against the original.

#![no_std]

pub mod fonts;
pub mod text;
pub mod umlaut;

// Re-exported for callers that only depend on pm-font; the types themselves
// belong to pm-core.
pub use pm_core::{Color, Display, Error, MockDisplay, Result};
pub use text::{draw_text, draw_text_len};

/// Rotation of the glyph bitmaps inside a font table.
///
/// Port of `font_rotation_t` in `lib/Platinenmacher/font.h`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Rotation {
    /// Rotate 0 degrees, clockwise.
    Rotate0 = 0,
    /// Rotate 90 degrees, clockwise.
    Rotate90 = 1,
    /// Rotate 180 degrees, clockwise.
    Rotate180 = 2,
    /// Rotate 270 degrees, clockwise.
    Rotate270 = 3,
}

impl Rotation {
    /// Decodes the rotation byte of a font table.
    ///
    /// Unknown values fall back to [`Rotation::Rotate0`], matching the C code,
    /// which only ever compares against `DISPLAY_ROTATE_90`.
    pub const fn from_u8(value: u8) -> Self {
        match value {
            1 => Rotation::Rotate90,
            2 => Rotation::Rotate180,
            3 => Rotation::Rotate270,
            _ => Rotation::Rotate0,
        }
    }
}

/// A monochrome bitmap font.
///
/// Port of `font_t`. A font table is a flat byte array whose first four bytes
/// are the header `[width, height, ascii_offset, rotation]`, followed by the
/// glyph bitmaps; [`Font::from_array`] performs the split that
/// `font_load_from_array()` does at runtime, but at compile time.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Font {
    data: &'static [u8],
    width: u8,
    height: u8,
    ascii_offset: u8,
    name: &'static str,
    rotation: Rotation,
}

impl Font {
    /// Splits a raw font table into header and glyph data.
    ///
    /// Port of `font_load_from_array()`. Unlike the C version this cannot
    /// fail: a table shorter than the four header bytes is rejected at compile
    /// time by the panic below.
    pub const fn from_array(raw: &'static [u8], name: &'static str) -> Self {
        if raw.len() < 4 {
            panic!("font table is shorter than its 4 byte header");
        }
        let (header, data) = raw.split_at(4);
        Font {
            data,
            width: header[0],
            height: header[1],
            ascii_offset: header[2],
            name,
            rotation: Rotation::from_u8(header[3]),
        }
    }

    /// Glyph width in pixels.
    pub const fn width(&self) -> u8 {
        self.width
    }

    /// Glyph height in pixels.
    pub const fn height(&self) -> u8 {
        self.height
    }

    /// First character the table has a glyph for (`font_t::asciiOffset`).
    pub const fn first_char(&self) -> u8 {
        self.ascii_offset
    }

    /// Last character the table has a glyph for.
    ///
    /// Derived from [`Font::glyph_count`], so it shares that method's C quirk.
    pub const fn last_char(&self) -> u8 {
        let count = self.glyph_count();
        if count == 0 {
            return self.ascii_offset;
        }
        let last = self.ascii_offset as usize + count - 1;
        if last > u8::MAX as usize {
            u8::MAX
        } else {
            last as u8
        }
    }

    /// Human readable font name, e.g. `"8x8"`.
    pub const fn name(&self) -> &'static str {
        self.name
    }

    /// Rotation the glyph bitmaps are stored in.
    pub const fn rotation(&self) -> Rotation {
        self.rotation
    }

    /// Glyph bitmaps, with the four header bytes stripped (`font_t::data`).
    pub const fn data(&self) -> &'static [u8] {
        self.data
    }

    /// Number of bytes the renderer reads per glyph.
    ///
    /// C quirk: `display_text_draw()` computes the glyph offset as
    /// `(c - asciiOffset) * height * width / 8`, i.e. it assumes the bitmap
    /// packs without padding. That holds for seven of the eight fonts; the
    /// 10x14 table stores 20 bytes per glyph while this formula yields 17, so
    /// the C renderer draws that font misaligned. The port keeps the formula
    /// so placement stays identical.
    pub const fn bytes_per_glyph(&self) -> usize {
        (self.width as usize * self.height as usize) / 8
    }

    /// Number of glyphs the table holds, as the renderer indexes them.
    pub const fn glyph_count(&self) -> usize {
        match self.data.len().checked_div(self.bytes_per_glyph()) {
            Some(count) => count,
            None => 0,
        }
    }

    /// Bitmap of a single character, or `None` if the font has no glyph for it.
    ///
    /// The C code performs no range check at all and happily reads past the
    /// end of the table for characters outside the font; returning `None` is
    /// the one deliberate behaviour change, and [`text::draw_text`] simply
    /// skips such characters.
    pub fn glyph(&self, c: u8) -> Option<&'static [u8]> {
        let stride = self.bytes_per_glyph();
        if stride == 0 || c < self.ascii_offset {
            return None;
        }
        let data = self.data;
        let start = (c - self.ascii_offset) as usize * stride;
        let end = start.checked_add(stride)?;
        if end > data.len() {
            return None;
        }
        Some(&data[start..end])
    }

    /// Width in pixels of `text` rendered with this font.
    ///
    /// Port of `font_text_pixel_width()`.
    ///
    /// C quirk: this multiplies by the glyph width, but the renderer advances
    /// a fixed 8 pixels per character (see [`text::draw_text`]), so the two
    /// only agree for the 8 pixel wide fonts.
    pub fn text_pixel_width(&self, text: &[u8]) -> u32 {
        self.width as u32 * strlen(text) as u32
    }

    /// Height in pixels of a single line rendered with this font.
    ///
    /// Port of `font_text_pixel_height()`.
    pub fn text_pixel_height(&self) -> u32 {
        self.height as u32
    }
}

/// Number of bytes before the first `NUL`, or the whole slice if there is none.
///
/// Port of `font_strlen()` for callers that still hold C style buffers.
pub fn strlen(text: &[u8]) -> usize {
    match text.iter().position(|&b| b == 0) {
        Some(n) => n,
        None => text.len(),
    }
}
