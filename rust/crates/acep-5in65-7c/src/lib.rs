//! Waveshare ACeP 5.65" 7-colour e-paper panel.
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.{c,h}.
//!
//! Generic over `embedded_hal` 1.0 `SpiDevice`/`OutputPin`/`InputPin`/`DelayNs`
//! and implements `pm_core::display::DisplayTarget` (behind the `pm-core`
//! feature, see [`display_target`]), so the pixel packing and the
//! decompression path are host-testable against a recording fake SPI bus.
//! The ESP32 SPI/GPIO glue it used to contain
//! (lib/Platinenmacher_HAL_ESP32/hw/esp32/{spi,gpio}.c) is gone: esp-idf-hal
//! already provides those embedded-hal impls in the firmware crate.
//!
//! # Command bytes are not reviewable against hardware
//!
//! A wrong init sequence is invisible without the panel in hand, so every
//! command byte, data byte and delay in [`command`] and [`driver`] carries a
//! `C:<line>` comment pointing at the line of `acep_5in65_7c.c` (or, for the
//! power rail, `src/esp32/gui.c`) it was copied from. Reviewing this crate
//! means diffing those comments against the C file, not reading a datasheet.
//!
//! # What was deliberately left out
//!
//! * `ACEP_5IN65_Display_part` (C:315-348) is dead code in the C tree --
//!   nothing calls it and it carries a `cppcheck-suppress unusedFunction`
//!   pragma. Porting untestable dead code was judged out of scope for this
//!   task; the framebuffer path it duplicates is [`driver::Acep5In65::update`].
//! * The SPI bus/device setup (C:175-197) has no counterpart: the caller
//!   hands the driver an already-configured `SpiDevice`. The C settings it
//!   replaces are 1 MHz, SPI mode 0, MISO unused.

#![no_std]
#![forbid(unsafe_code)]

extern crate alloc;

#[cfg(test)]
extern crate std;

pub mod command;
#[cfg(feature = "pm-core")]
pub mod display_target;
pub mod driver;

#[cfg(test)]
mod mock;

pub use driver::{Acep5In65, Error, Rotation, FB_SIZE, HEIGHT, WIDTH};
