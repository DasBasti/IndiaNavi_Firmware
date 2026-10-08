//! IndiaNavi firmware entry point.
//!
//! Replaces: src/esp32/main.c (and eventually the rest of src/esp32/* plus
//! src/screens/*).
//!
//! This is the only crate in the port allowed to name concrete hardware. It
//! constructs the esp-idf-hal peripherals, hands them to the drivers as
//! `embedded-hal` implementations, and owns the FreeRTOS tasks. Everything it
//! drives lives in ../crates and is host-testable without it.
//!
//! Stub: no logic has been ported yet.

fn main() {
    // Required once before any other esp-idf-svc call: applies the runtime
    // patches that the ESP-IDF needs when driven from Rust.
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    log::info!("IndiaNavi {} starting", env!("CARGO_PKG_VERSION"));
    log::info!("board: {}", BOARD);

    // Ported subsystems get wired up here: regulators, SPI + e-paper panel,
    // I2C + LSM303, UART + NMEA, SD/FATFS, WiFi, HTTPS OTA, NVS, sleep.
}

/// Board this image was built for, selected by the `board-*` cargo features.
#[cfg(feature = "board-esp32s3")]
pub const BOARD: &str = "indianavi-s3-n16r8";
#[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
pub const BOARD: &str = "esp32dev-rev2";

#[cfg(not(any(feature = "board-esp32s3", feature = "board-esp32")))]
compile_error!("enable exactly one board feature: board-esp32s3 (default) or board-esp32");
