//! Core types of the Platinenmacher library.
//!
//! Ported from C, replacing:
//!
//! | C file                                | Rust module      |
//! |---------------------------------------|------------------|
//! | `lib/Platinenmacher/error.h`           | [`error`]        |
//! | `lib/Platinenmacher/colors.h`          | [`colors`]       |
//! | `lib/Platinenmacher/gui/geometric.h`   | [`geometric`]    |
//! | `lib/Platinenmacher/memory.h`          | [`memory`]       |
//! | `lib/Platinenmacher/display.h`, `display.c` | [`display`] |
//! | `test/host/Platinenmacher/mock/mock_display.h` | [`mock`] (feature `mock`) |
//!
//! The crate is `no_std` + `alloc`; `std` is only used by its own unit tests.
//! `display_text_draw()`/`display_text_draw_len()` are *not* here: they need
//! `font_t` and are ported together with `lib/Platinenmacher/font.c` (pm-font).

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

extern crate alloc;

pub mod colors;
pub mod display;
pub mod error;
pub mod geometric;
pub mod memory;

#[cfg(any(test, feature = "mock"))]
pub mod mock;

pub use colors::Color;
pub use display::{sizeof_fb, Display, DisplayTarget, FrameBuffer, Rotation};
pub use error::{Error, Result};
pub use geometric::{Point, Rect};
