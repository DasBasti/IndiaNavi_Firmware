//! Board support: the only place in the port that names concrete hardware.
//!
//! Replaces: include/pins.h, lib/Platinenmacher_HAL_ESP32/hw/gpio.h,
//! lib/Platinenmacher_HAL_ESP32/hw/spi.h,
//! lib/Platinenmacher_HAL_ESP32/hw/regulator.h,
//! lib/Platinenmacher_HAL_ESP32/hw/regulator_gpio.h,
//! lib/Platinenmacher_HAL_ESP32/hw/esp32/{gpio.c, spi.c, regulator_gpio.c},
//! and the peripheral setup scattered through src/esp32/main.c
//! (`readBatteryPercent`, the ADC and button configuration, the LED)
//!
//! # What this module is for
//!
//! `rust/PORTING.md` states the rule the whole port hangs on: **hardware is
//! only ever reached through `embedded-hal` 1.0 traits.** The driver crates
//! under `rust/crates/` take a `SpiDevice`, an `I2c`, an `OutputPin`; they never
//! learn which chip they are on. This module is the other side of that seam --
//! it turns a specific board into those traits and stops.
//!
//! So the crates stay host-testable, and the part of the board that is *logic*
//! rather than register pokes stays host-testable too.
//!
//! # Layout
//!
//! | Module | What it holds | Host-testable |
//! | --- | --- | --- |
//! | [`pins`] | both boards' pin maps, `include/pins.h` | yes, both boards at once |
//! | [`gpio`] | levels, active-level tests, pin constructors | the pure half |
//! | [`regulator`] | the three active-low power rails, refcounted | yes, in full |
//! | [`spi`] | ePaper SPI bus and SD (SDMMC) bus descriptors | the descriptors |
//! | [`i2c`] | the LSM303 bus | the parameters |
//! | [`adc`] | `VBAT_ADC` / `VIN_ADC` channels and the one-shot driver | the channel map |
//! | [`battery`] | `readBatteryPercent`, as a pure function | yes, in full |
//! | [`button`] | `BTN` with `BTN_LEVEL` | yes, in full |
//! | [`led`] | the status LED | yes, in full |
//!
//! # The `cfg(target_os = "espidf")` split
//!
//! Anything that needs ESP-IDF lives in an inner `mod espidf` gated on
//! `cfg(target_os = "espidf")` and re-exported from its parent. Everything
//! outside those inner modules compiles for the host, which is what lets
//! `cargo test --target x86_64-unknown-linux-gnu` run the tests in this tree
//! without the Xtensa toolchain -- see rust/firmware/TESTING.md for the exact
//! commands and for what this does and does not verify. `Cargo.toml` puts
//! `esp-idf-hal`/`esp-idf-svc` under
//! `[target.'cfg(target_os = "espidf")'.dependencies]` so a host build does not
//! even resolve them.
//!
//! The consequence, stated plainly for reviewers: the `espidf` submodules are
//! **not compiled anywhere in CI or in this container** (rust/PORTING.md,
//! "Firmware build", explains why the ESP-IDF build cannot run here). They are
//! deliberately thin -- config structs and one constructor each -- and every
//! value they pass to ESP-IDF comes from a `const` that *is* covered by a host
//! test. Review them by reading; review the numbers by running the tests.
//!
//! # Boards
//!
//! `pins.h`'s `#ifdef ESP_S3` becomes the `board-esp32s3` (default) and
//! `board-esp32` cargo features. Both pin tables are always compiled (see
//! [`pins`]), and the feature only picks which one [`pins::PINS`] means.

pub mod adc;
pub mod battery;
pub mod button;
pub mod gpio;
pub mod i2c;
pub mod led;
pub mod pins;
pub mod regulator;
pub mod spi;

#[cfg(test)]
pub mod mock;

pub use battery::{BatteryStatus, ChargeEvent};
pub use button::{Button, ButtonEvent};
pub use gpio::PinLevel;
pub use led::Led;
pub use pins::{PinMap, PINS};
pub use regulator::{GpioRegulator, Regulator, RegulatorStatus};

/// The three GPIO-switched power rails, named as `include/pins.h` names them.
///
/// Every one is active low, so [`GpioRegulator::active_low`] is the only
/// constructor any of them should ever be built with. See
/// [`regulator`] for the C call sites.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Rail {
    /// `GPS_VCC_nEN` -- the L96 GPS module (src/esp32/gps.c:158).
    Gps,
    /// `EINK_VCC_nEN` -- the ePaper panel (src/esp32/gui.c:358).
    Eink,
    /// `SD_VCC_nEN` -- the SD card (src/esp32/sd.c:260).
    Sd,
}

impl Rail {
    /// Enable pin for this rail on the board being built.
    #[must_use]
    pub const fn pin(self) -> i32 {
        self.pin_on(PINS)
    }

    /// Enable pin for this rail on an arbitrary board, so both can be tested.
    #[must_use]
    pub const fn pin_on(self, pins: PinMap) -> i32 {
        match self {
            Rail::Gps => pins.gps_vcc_nen,
            Rail::Eink => pins.eink_vcc_nen,
            Rail::Sd => pins.sd_vcc_nen,
        }
    }

    /// The level that switches this rail on. Always [`PinLevel::Reset`]: the
    /// `nEN` in every name is not decoration.
    #[must_use]
    pub const fn on_level(self) -> PinLevel {
        PinLevel::Reset
    }
}

#[cfg(test)]
mod tests {
    use super::pins::{ESP32S3, ESP32_REV2};
    use super::*;

    #[test]
    fn rail_pins_match_pins_h_on_esp32s3() {
        assert_eq!(Rail::Gps.pin_on(ESP32S3), 15);
        assert_eq!(Rail::Eink.pin_on(ESP32S3), 18);
        assert_eq!(Rail::Sd.pin_on(ESP32S3), 14);
    }

    #[test]
    fn rail_pins_match_pins_h_on_esp32_rev2() {
        assert_eq!(Rail::Gps.pin_on(ESP32_REV2), 32);
        assert_eq!(Rail::Eink.pin_on(ESP32_REV2), 26);
        assert_eq!(Rail::Sd.pin_on(ESP32_REV2), 16);
    }

    #[test]
    fn every_rail_is_active_low() {
        for rail in [Rail::Gps, Rail::Eink, Rail::Sd] {
            assert_eq!(rail.on_level(), PinLevel::Reset);
        }
    }

    #[test]
    fn no_two_rails_share_an_enable_pin() {
        for pins in [ESP32S3, ESP32_REV2] {
            let p = [
                Rail::Gps.pin_on(pins),
                Rail::Eink.pin_on(pins),
                Rail::Sd.pin_on(pins),
            ];
            assert_ne!(p[0], p[1]);
            assert_ne!(p[1], p[2]);
            assert_ne!(p[0], p[2]);
        }
    }

    #[test]
    fn the_active_board_rails_come_from_the_active_pin_map() {
        assert_eq!(Rail::Gps.pin(), PINS.gps_vcc_nen);
        assert_eq!(Rail::Eink.pin(), PINS.eink_vcc_nen);
        assert_eq!(Rail::Sd.pin(), PINS.sd_vcc_nen);
    }

    /// The rails, the buses and the button/LED must not collide -- a pin map
    /// typo that put the SD rail on the ePaper clock would otherwise only show
    /// up as a dead panel on the bench.
    #[test]
    fn nothing_shares_a_pin_with_anything_else() {
        for (pins, s3) in [(ESP32S3, true), (ESP32_REV2, false)] {
            let eink = spi::eink_bus(pins);
            let sd = spi::sd_bus(pins, s3);
            let used = [
                ("gps rail", Rail::Gps.pin_on(pins)),
                ("eink rail", Rail::Eink.pin_on(pins)),
                ("sd rail", Rail::Sd.pin_on(pins)),
                ("eink sclk", eink.sclk),
                ("eink mosi", eink.mosi),
                ("eink cs", eink.cs),
                ("eink dc", pins.eink_dc),
                ("eink busy", pins.eink_busy),
                ("sd clk", sd.clk),
                ("sd cmd", sd.cmd),
                ("sd d0", sd.d0),
                ("sd d1", sd.d1),
                ("sd d2", sd.d2),
                ("sd d3", sd.d3),
                ("sd ndet", pins.sd_card_ndet),
                ("btn", pins.btn),
                ("led", pins.led),
                ("gps tx", pins.gps_uart2_tx),
                ("gps rx", pins.gps_uart2_rx),
                ("i2c scl", pins.i2c_scl),
                ("i2c int", pins.i2c_int),
            ];
            for (i, (name_a, a)) in used.iter().enumerate() {
                for (name_b, b) in used.iter().skip(i + 1) {
                    assert_ne!(a, b, "{name_a} and {name_b} are both GPIO {a}");
                }
            }
        }
    }
}
