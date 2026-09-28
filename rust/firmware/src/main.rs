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
//! Stub: no application logic has been ported yet. The board support layer in
//! [`board`] is the first thing to land here.

// Board support is infrastructure for the tasks that have not been ported yet,
// so almost none of it has a caller in this file. Without this the whole module
// is one long `dead_code` warning; the host tests are what keep it honest in the
// meantime. Remove this once `main` actually wires the subsystems up.
#[allow(dead_code)]
pub mod board;

/// Board this image was built for, selected by the `board-*` cargo features.
#[cfg(feature = "board-esp32s3")]
pub const BOARD: &str = "indianavi-s3-n16r8";
#[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
pub const BOARD: &str = "esp32dev-rev2";

#[cfg(not(any(feature = "board-esp32s3", feature = "board-esp32")))]
compile_error!("enable exactly one board feature: board-esp32s3 (default) or board-esp32");

#[cfg(target_os = "espidf")]
fn main() {
    // Required once before any other esp-idf-svc call: applies the runtime
    // patches that the ESP-IDF needs when driven from Rust.
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    log::info!("IndiaNavi {} starting", env!("CARGO_PKG_VERSION"));
    log::info!("board: {}", BOARD);

    // Ported subsystems get wired up here: regulators, SPI + e-paper panel,
    // I2C + LSM303, UART + NMEA, SD/FATFS, WiFi, HTTPS OTA, NVS, sleep.
    // `board` already provides the pin maps, bus descriptors, rails, ADC,
    // button and LED they need.
}

/// Host builds exist only so `cargo test` can run the pure logic in [`board`]
/// without the Xtensa toolchain -- see TESTING.md. There is no host port of the
/// firmware itself and there is not meant to be.
#[cfg(not(target_os = "espidf"))]
fn main() {
    eprintln!(
        "indianavi-firmware is an ESP-IDF binary and does nothing on the host.\n\
         This target exists so `cargo test` can run the host-testable parts of\n\
         src/board/. Build the real thing with:\n\
         \n\
         \x20   cargo build --release            # {BOARD} (ESP32-S3)\n\
         \x20   cargo build --release --target xtensa-esp32-espidf \\\n\
         \x20       --no-default-features --features board-esp32\n"
    );
    std::process::exit(1);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exactly_one_board_is_selected() {
        // `compile_error!` above covers "neither". This covers "the name
        // matches the map", which is the mistake a copy-paste would make.
        #[cfg(feature = "board-esp32s3")]
        {
            assert_eq!(BOARD, "indianavi-s3-n16r8");
            assert_eq!(board::PINS, board::pins::ESP32S3);
        }
        #[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
        {
            assert_eq!(BOARD, "esp32dev-rev2");
            assert_eq!(board::PINS, board::pins::ESP32_REV2);
        }
    }
}
