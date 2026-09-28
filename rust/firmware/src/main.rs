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
//! Ported so far: [`runtime`] — the concurrency scaffolding from
//! `include/tasks.h` and the shared globals from `include/gui.h`. No
//! peripheral has been wired up yet, so `main` still only logs.
//!
//! # Host build
//!
//! `runtime` is deliberately free of ESP-IDF, so its tests run on the host:
//!
//! ```sh
//! cd rust/firmware
//! cargo test --target x86_64-unknown-linux-gnu
//! ```
//!
//! (The explicit `--target` is needed once the scaffold's
//! `.cargo/config.toml`, which pins `xtensa-esp32s3-espidf`, is merged
//! alongside this.)

// `static mut` and every other route to unsynchronised global mutable state
// needs `unsafe` to read or write, so denying `unsafe_code` crate-wide is what
// mechanically enforces the rule stated in `runtime`'s module header. Lift it
// only with a comment naming the peripheral that forced it.
#![deny(unsafe_code)]

pub mod runtime;

fn main() {
    #[cfg(target_os = "espidf")]
    {
        // Required once before any other esp-idf-svc call: applies the runtime
        // patches that the ESP-IDF needs when driven from Rust.
        esp_idf_svc::sys::link_patches();
        esp_idf_svc::log::EspLogger::initialize_default();
    }

    log::info!("IndiaNavi {} starting", env!("CARGO_PKG_VERSION"));
    log::info!("board: {BOARD}");

    // Replaces src/esp32/main.c:276-278.
    let (_context, _events) = runtime::init();

    // Ported subsystems get wired up here: regulators, SPI + e-paper panel,
    // I2C + LSM303, UART + NMEA, SD/FATFS, WiFi, HTTPS OTA, NVS, sleep. Each
    // one takes a `runtime::Context` clone and a `runtime::TaskSpec` from
    // `runtime::Tasks`, and the main loop then drains `_events` the way
    // src/esp32/main.c:357-387 does.
}

/// Board this image was built for, selected by the `board-*` cargo features.
#[cfg(feature = "board-esp32s3")]
pub const BOARD: &str = "indianavi-s3-n16r8";
#[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
pub const BOARD: &str = "esp32dev-rev2";

#[cfg(not(any(feature = "board-esp32s3", feature = "board-esp32")))]
compile_error!("enable exactly one board feature: board-esp32s3 (default) or board-esp32");
