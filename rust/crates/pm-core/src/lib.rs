//! Core types of the Platinenmacher library.
//!
//! Replaces: lib/Platinenmacher/{error.h, colors.h, memory.h, rtos.h,
//! display.c, display.h, gps.c, gps.h}, lib/Platinenmacher/gui/geometric.h,
//! lib/Platinenmacher_HAL_ESP32/hw/regulator*.{c,h} and lib/helper/*.
//!
//! `memory.h` (RTOS_Malloc/RTOS_Free) and `rtos.h` have no counterpart: the
//! port uses the Rust allocator and ownership instead of hand-rolled
//! zeroing allocators.
//!
//! Nothing in this crate touches hardware. Peripherals reach the port only
//! through `embedded-hal` 1.0 traits and the `DisplayTarget` trait in
//! [`display`], so the whole crate builds and unit-tests on the host.

pub mod color;
pub mod display;
pub mod error;
pub mod geometry;
pub mod gps;
pub mod helper;
pub mod power;

pub use error::{Error, Result};
