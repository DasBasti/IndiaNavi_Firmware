//! Bitmap fonts.
//!
//! Replaces: lib/Platinenmacher/font.c, lib/Platinenmacher/font.h and
//! lib/Platinenmacher/fonts/*.
//!
//! Host-testable: the font tables are plain `const` data and the metrics are
//! pure functions.

pub mod builtin;
pub mod font;
