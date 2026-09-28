//! The I2C bus the LSM303 accelerometer/magnetometer sits on.
//!
//! Replaces: the `i2c_config_t` / `i2c_param_config` / `i2c_driver_install`
//! block inside `lsm303_init` (lib/lsm303/lsm303.c:143-159), and the
//! `I2C_MASTER_NUM` / `I2C_SDA` / `I2C_SCL` / `I2C_INT` / `I2C_INT_LEVEL`
//! defines of include/pins.h
//!
//! The C driver configured the bus *inside itself*, which is why the Rust
//! `lsm303` crate -- generic over `embedded_hal::i2c::I2c` -- has nowhere to put
//! that code. Bus setup is a board concern, so it lives here, and the driver
//! only ever sees a bus that already works. `esp-idf-hal`'s `I2cDriver`
//! implements `embedded_hal::i2c::I2c`, so `lsm303::Lsm303::new(i2c)` takes the
//! handle [`i2c_bus`] returns with no adapter in between.
//!
//! `I2C_INT` is the accelerometer's tap interrupt, active low
//! (`I2C_INT_LEVEL == 0`). `src/esp32/main.c:290-297` wired it to the same
//! any-edge handler as the button, behind `#ifdef WITH_ACC`; the pin handle for
//! it comes from [`super::gpio::input_pullup`] and the level test from
//! [`super::gpio::is_asserted`].

use super::gpio::PinLevel;
use super::pins::PINS;

/// Bus clock, `conf.master.clk_speed` (lib/lsm303/lsm303.c:153): 400 kHz, I2C
/// fast mode.
pub const CLOCK_HZ: u32 = 400_000;

/// I2C peripheral index, `I2C_MASTER_NUM`. 0 on both boards.
pub const MASTER_NUM: i32 = PINS.i2c_master_num;

/// Whether the internal pull-ups are enabled on SDA and SCL.
///
/// `true`: lib/lsm303/lsm303.c:150 and :152 both set `GPIO_PULLUP_ENABLE`. The
/// board has external pull-ups as well, so this is belt and braces, but
/// changing it is a hardware question and not a porting one.
pub const INTERNAL_PULLUPS: bool = true;

// Asserted at compile time rather than in a #[test] because clippy rejects a
// runtime assertion on constants.
const _: () = assert!(INTERNAL_PULLUPS);

/// Level `I2C_INT` asserts at, from `I2C_INT_LEVEL`.
pub const INT_ACTIVE_LEVEL: PinLevel = PinLevel::from_pins_h(PINS.i2c_int_level);

/// `esp-idf-hal` construction of the I2C master.
#[cfg(target_os = "espidf")]
pub use espidf::*;

#[cfg(target_os = "espidf")]
mod espidf {
    //! UNVERIFIED BY A COMPILER -- see rust/PORTING.md "Firmware build".
    //! Written against `esp-idf-hal` 0.47.0; see TESTING.md.

    use esp_idf_hal::gpio::{InputPin as EspInputPin, OutputPin as EspOutputPin};
    use esp_idf_hal::i2c::{I2c, I2cConfig, I2cDriver};
    use esp_idf_hal::sys::EspError;
    use esp_idf_hal::units::Hertz;

    use super::{CLOCK_HZ, INTERNAL_PULLUPS};

    /// The bus handle. Implements `embedded_hal::i2c::I2c`, which is what the
    /// `lsm303` driver is generic over.
    pub type Bus<'d> = I2cDriver<'d>;

    /// Bring up the I2C master at 400 kHz with pull-ups on, as `lsm303_init`
    /// did.
    ///
    /// Peripherals are taken by value: `esp-idf-hal` 0.47 dropped the
    /// `Peripheral<P = ...>` indirection, so this is `peripherals.i2c0` and
    /// two pins straight from `peripherals.pins`.
    ///
    /// Deviation: C called `ESP_ERROR_CHECK` on both setup calls, aborting the
    /// firmware if the bus could not be configured. The accelerometer is
    /// optional on this board -- its only call site in `src/esp32/main.c:339`
    /// is commented out -- so a failure here is returned and the caller can
    /// carry on without it.
    pub fn i2c_bus<'d>(
        i2c: impl I2c + 'd,
        sda: impl EspInputPin + EspOutputPin + 'd,
        scl: impl EspInputPin + EspOutputPin + 'd,
    ) -> Result<Bus<'d>, EspError> {
        let config = I2cConfig::new()
            .baudrate(Hertz(CLOCK_HZ))
            .sda_enable_pullup(INTERNAL_PULLUPS)
            .scl_enable_pullup(INTERNAL_PULLUPS);
        I2cDriver::new(i2c, sda, scl, &config)
    }

    /// Compile-time check that the accelerometer gets an
    /// `embedded_hal::i2c::I2c`, which is what `lsm303` is generic over.
    /// Never called.
    #[allow(dead_code)]
    fn assert_embedded_hal_1_0(bus: Bus<'static>) {
        fn takes_i2c(_: impl embedded_hal::i2c::I2c) {}
        takes_i2c(bus);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::gpio::is_asserted;
    use crate::board::pins::{ESP32S3, ESP32_REV2};

    #[test]
    fn clock_matches_the_c_driver() {
        assert_eq!(CLOCK_HZ, 400_000, "lib/lsm303/lsm303.c:153");
    }

    #[test]
    fn peripheral_index_is_zero_on_both_boards() {
        assert_eq!(ESP32S3.i2c_master_num, 0);
        assert_eq!(ESP32_REV2.i2c_master_num, 0);
        assert_eq!(MASTER_NUM, 0);
    }

    #[test]
    fn the_accelerometer_interrupt_is_active_low_on_both_boards() {
        assert_eq!(
            PinLevel::from_pins_h(ESP32S3.i2c_int_level),
            PinLevel::Reset
        );
        assert_eq!(
            PinLevel::from_pins_h(ESP32_REV2.i2c_int_level),
            PinLevel::Reset
        );
        assert_eq!(INT_ACTIVE_LEVEL, PinLevel::Reset);
        // A low reading means "tap", which is main.c:183's
        // `gpio_get_level(I2C_INT) == I2C_INT_LEVEL`.
        assert!(is_asserted(PinLevel::Reset, INT_ACTIVE_LEVEL));
        assert!(!is_asserted(PinLevel::Set, INT_ACTIVE_LEVEL));
    }
}
