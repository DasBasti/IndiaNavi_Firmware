//! Core types of the Platinenmacher display stack.
//!
//! This is the slice of `lib/Platinenmacher` that every other crate of the
//! port builds on:
//!
//! * `error.h` -- see [`error`],
//! * the framebuffer surface of `display.h` / `display.c`
//!   (`display_t`, `display_pixel_draw()`) -- see [`display`].
//!
//! Only what the text renderer needs is ported here; the geometric drawing
//! primitives of `display.c` stay in C for now.

#![no_std]

extern crate alloc;

pub mod display;
pub mod error;

pub use display::{in_bounds, Color, Display, MockDisplay};
pub use error::{Error, Result};
