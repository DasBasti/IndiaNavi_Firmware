//! GPIO levels, active-level handling, and the `embedded-hal` pin handles the
//! drivers are built from.
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/hw/gpio.h,
//! lib/Platinenmacher_HAL_ESP32/hw/esp32/gpio.c
//!
//! The C `gpio_t` carried a direction, a port, a pin number, an `onValue` and a
//! push-pull mode, and `gpio_write`/`gpio_read` dispatched on the direction at
//! every call. None of that survives: `esp-idf-hal`'s `PinDriver` already
//! implements `embedded_hal::digital::{InputPin, OutputPin}`, and the direction
//! is a type parameter, so "write to an input pin" -- silently a no-op in
//! `gpio.c:41` -- stops compiling instead.
//!
//! What *does* need porting is the `onValue` idea: several lines on this board
//! are active low (`GPS_VCC_nEN`, `EINK_VCC_nEN`, `SD_VCC_nEN`, `BTN`,
//! `SD_CARD_nDET`, `I2C_INT`). That becomes [`PinLevel`] plus the small pure
//! helpers below, which [`super::regulator`] and [`super::button`] build on.

/// A raw digital level.
///
/// Replaces `gpio_value_t` (`gpio.h:13-17`); the discriminants are pinned to
/// the C enum so `GPIO_RESET`/`GPIO_SET` keep meaning 0/1 while the C tree is
/// still around.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PinLevel {
    /// `GPIO_RESET`: low.
    Reset = 0,
    /// `GPIO_SET`: high.
    Set = 1,
}

impl PinLevel {
    /// The other level.
    ///
    /// This is what `gpio_write(gpio, !gpio->onValue)` in
    /// `regulator_gpio.c:32` computes: C's `!` on a `gpio_value_t` yields 1 for
    /// `GPIO_RESET` and 0 for `GPIO_SET`.
    #[must_use]
    pub const fn inverted(self) -> Self {
        match self {
            PinLevel::Reset => PinLevel::Set,
            PinLevel::Set => PinLevel::Reset,
        }
    }

    /// `true` for [`PinLevel::Set`]. The form `embedded-hal` wants.
    #[must_use]
    pub const fn is_high(self) -> bool {
        matches!(self, PinLevel::Set)
    }

    /// Build a level from a `bool`, the form `embedded-hal` reads back.
    #[must_use]
    pub const fn from_high(high: bool) -> Self {
        if high {
            PinLevel::Set
        } else {
            PinLevel::Reset
        }
    }

    /// Build a level from a `pins.h` `*_LEVEL` constant (`BTN_LEVEL`,
    /// `I2C_INT_LEVEL`), which C spells as a plain `0`/`1`.
    ///
    /// Anything other than 0 is high, matching C truthiness.
    #[must_use]
    pub const fn from_pins_h(value: i32) -> Self {
        if value == 0 {
            PinLevel::Reset
        } else {
            PinLevel::Set
        }
    }
}

/// Is a line asserted, given the level it asserts at?
///
/// This is the comparison `src/esp32/main.c:186` spells as
/// `gpio_get_level(BTN) == BTN_LEVEL`, and `src/esp32/sd.c:271` spells as
/// `!gpio_read(dc_dt)` for the active-low `SD_CARD_nDET`.
#[must_use]
pub const fn is_asserted(read: PinLevel, active_level: PinLevel) -> bool {
    read as u8 == active_level as u8
}

/// The concrete `embedded-hal` pin handles, and the peripherals they come from.
///
/// Everything above this line is pure and host-testable. Everything below needs
/// ESP-IDF, so it is compiled only for the device.
#[cfg(target_os = "espidf")]
pub use espidf::*;

#[cfg(target_os = "espidf")]
mod espidf {
    //! Thin wrappers that turn an `esp-idf-hal` pin into an `embedded-hal` 1.0
    //! pin. `PinDriver` already implements the traits; these functions exist
    //! only to keep the pull/interrupt configuration of the C code in one
    //! place.
    //!
    //! UNVERIFIED BY A COMPILER: this container cannot build for ESP-IDF (no
    //! host C compiler, no python3/cmake/ninja -- see rust/PORTING.md
    //! "Firmware build"). Reviewers should treat this module as inspection
    //! only; the pure modules around it are covered by host tests.

    use esp_idf_hal::gpio::{AnyIOPin, AnyInputPin, AnyOutputPin, Input, Output, PinDriver, Pull};
    use esp_idf_hal::sys::EspError;

    use super::PinLevel;

    /// An output pin. Implements `embedded_hal::digital::OutputPin`.
    pub type OutPin = PinDriver<'static, AnyOutputPin, Output>;
    /// An input pin. Implements `embedded_hal::digital::InputPin`.
    pub type InPin = PinDriver<'static, AnyInputPin, Input>;
    /// An input pin that can also raise interrupts, so it needs `AnyIOPin`.
    pub type IrqPin = PinDriver<'static, AnyIOPin, Input>;

    /// Configure a pin as a push-pull output driven to `initial`.
    ///
    /// Replaces `gpio_create(OUTPUT, 0, pin)` (`gpio.c:14-37`) plus the
    /// `gpio_write` that every call site immediately performed. The C version
    /// left the pin at whatever the reset state was until the first write; here
    /// the caller must say, because a power rail that floats between
    /// `gpio_config` and the first `gpio_write` is how `EINK_VCC_nEN` gets to
    /// glitch the panel.
    pub fn output(pin: AnyOutputPin, initial: PinLevel) -> Result<OutPin, EspError> {
        let mut driver = PinDriver::output(pin)?;
        driver.set_level(if initial.is_high() {
            esp_idf_hal::gpio::Level::High
        } else {
            esp_idf_hal::gpio::Level::Low
        })?;
        Ok(driver)
    }

    /// Configure a pin as a floating input.
    ///
    /// Replaces `gpio_create(INPUT, 0, pin)`, used for `SD_CARD_nDET`
    /// (`src/esp32/sd.c:267`). The card-detect line has an external pull-up on
    /// both boards, which is why the C code asked for no pull either.
    pub fn input(pin: AnyInputPin) -> Result<InPin, EspError> {
        PinDriver::input(pin)
    }

    /// Configure a pin as an input with the internal pull-up enabled.
    ///
    /// Replaces the `esp_btn`/`esp_acc` `gpio_config_t` in
    /// `src/esp32/main.c:249-251` and `:290-292`, which set
    /// `pull_up_en = true`. Interrupt type is left to the caller
    /// ([`super::button`] wants any-edge, the accelerometer wants neg-edge).
    pub fn input_pullup(pin: AnyIOPin) -> Result<IrqPin, EspError> {
        let mut driver = PinDriver::input(pin)?;
        driver.set_pull(Pull::Up)?;
        Ok(driver)
    }
}
