//! Compile-time 32x32 status icons.
//!
//! Replaces: lib/icons_32/* (including icons_32.h, whose `extern uint8_t[]`
//! declarations become `pub const` slices here).
//!
//! Pure data, so it builds and tests on the host.

pub mod battery;
pub mod gps;
pub mod misc;
pub mod sd;
pub mod wifi;

/// Edge length of every icon in pixels (`ICON_SIZE` in icons_32.h).
pub const ICON_SIZE: u16 = 32;
