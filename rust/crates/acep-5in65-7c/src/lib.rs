//! Waveshare ACeP 5.65" 7-colour e-paper panel.
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.{c,h}.
//!
//! Generic over `embedded_hal` SPI/OutputPin/InputPin/DelayNs and implements
//! `pm_core::display::DisplayTarget`, so the pixel packing and the
//! decompression path are host-testable against a recording fake SPI bus.
//! The ESP32 SPI/GPIO glue it used to contain
//! (lib/Platinenmacher_HAL_ESP32/hw/esp32/{spi,gpio}.c) is gone: esp-idf-hal
//! already provides those embedded-hal impls in the firmware crate.

pub mod command;
pub mod driver;
