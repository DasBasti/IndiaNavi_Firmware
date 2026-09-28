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
    //! only to keep the pull configuration and the initial level of the C code
    //! in one place.
    //!
    //! UNVERIFIED BY A COMPILER: this container cannot build for ESP-IDF (no
    //! host C compiler, no python3/cmake/ninja -- see rust/PORTING.md
    //! "Firmware build"). It *has* been checked line by line against the
    //! published source of `esp-idf-hal` 0.47.0, the version `Cargo.toml`
    //! resolves to; see TESTING.md, "What the espidf blocks were checked
    //! against".
    //!
    //! Two things about that API are easy to get wrong, because most examples
    //! on the internet predate them:
    //!
    //! - `PinDriver` is generic over the *mode* only (`PinDriver<'d, MODE>`).
    //!   The pin type is erased at construction, so there is no third
    //!   parameter to name.
    //! - Peripherals are passed **by value** and carry a lifetime
    //!   (`AnyOutputPin<'d>`, `Gpio16<'d>`). The `esp_idf_hal::peripheral`
    //!   module and its `Peripheral<P = ...>` trait are gone.

    use esp_idf_hal::gpio::{
        Input, InputPin as EspInputPin, Level, Output, OutputPin as EspOutputPin, PinDriver, Pull,
    };
    use esp_idf_hal::sys::EspError;

    use super::PinLevel;

    /// An output pin. Implements `embedded_hal::digital::OutputPin`, which is
    /// all [`super::super::regulator::GpioRegulator`], [`super::super::led::Led`]
    /// and the panel's D/C line ever ask for.
    pub type OutPin<'d> = PinDriver<'d, Output>;
    /// An input pin. Implements `embedded_hal::digital::InputPin`, which is what
    /// [`super::super::button::Button`] and the panel's BUSY line ask for.
    pub type InPin<'d> = PinDriver<'d, Input>;
    /// An input pin that is also going to raise interrupts. Same type as
    /// [`InPin`] -- `set_interrupt_type`/`subscribe` are available on any input
    /// driver -- named separately only because the call sites read better.
    pub type IrqPin<'d> = InPin<'d>;

    /// [`PinLevel`] as `esp-idf-hal` spells it.
    #[must_use]
    pub const fn level(level: PinLevel) -> Level {
        match level {
            PinLevel::Set => Level::High,
            PinLevel::Reset => Level::Low,
        }
    }

    /// Configure a pin as a push-pull output driven to `initial`.
    ///
    /// Replaces `gpio_create(OUTPUT, 0, pin)` (`gpio.c:14-37`) plus the
    /// `gpio_write` that every call site immediately performed. The C version
    /// left the pin at whatever the reset state was until the first write; here
    /// the caller must say, because a power rail that floats between
    /// `gpio_config` and the first `gpio_write` is how `EINK_VCC_nEN` gets to
    /// glitch the panel.
    ///
    /// `gpio.c` also carried a `gpio_pp_mode_t` (`PUSHPULL`/`OPENDRAIN`) that
    /// it never once read -- `gpio_config` was always left in push-pull -- so
    /// open drain is not offered here. `PinDriver::output_od` is there if a
    /// line ever needs it.
    pub fn output<'d>(
        pin: impl EspOutputPin + 'd,
        initial: PinLevel,
    ) -> Result<OutPin<'d>, EspError> {
        let mut driver = PinDriver::output(pin)?;
        driver.set_level(level(initial))?;
        Ok(driver)
    }

    /// Configure a pin as a floating input.
    ///
    /// Replaces `gpio_create(INPUT, 0, pin)`, used for `SD_CARD_nDET`
    /// (`src/esp32/sd.c:267`) and `EINK_BUSY`. `gpio.c` never touched the pull
    /// registers, which is ESP-IDF's floating default, so that is what this
    /// asks for explicitly -- 0.47's `PinDriver::input` takes the pull as an
    /// argument and has no "leave it alone" option.
    pub fn input<'d>(pin: impl EspInputPin + 'd) -> Result<InPin<'d>, EspError> {
        PinDriver::input(pin, Pull::Floating)
    }

    /// Configure a pin as an input with the internal pull-up enabled.
    ///
    /// Replaces the `esp_btn`/`esp_acc` `gpio_config_t` in
    /// `src/esp32/main.c:249-251` and `:290-292`, which set
    /// `pull_up_en = true`. Interrupt type is left to the caller
    /// ([`super::super::button`] wants any-edge, the accelerometer wants
    /// neg-edge), because `PinDriver::set_interrupt_type` is a separate call.
    pub fn input_pullup<'d>(pin: impl EspInputPin + 'd) -> Result<IrqPin<'d>, EspError> {
        PinDriver::input(pin, Pull::Up)
    }

    /// Compile-time check that this module really does hand out
    /// `embedded-hal` 1.0 pins, which is the whole point of it: the drivers
    /// under `rust/crates/` take those traits and nothing else. Never called;
    /// it exists so a device build fails here rather than inside a driver.
    #[allow(dead_code)]
    fn assert_embedded_hal_1_0(out: OutPin<'static>, input: InPin<'static>) {
        fn takes_output_pin(_: impl embedded_hal::digital::OutputPin) {}
        fn takes_input_pin(_: impl embedded_hal::digital::InputPin) {}
        takes_output_pin(out);
        takes_input_pin(input);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn inverted_matches_c_logical_not_on_a_gpio_value() {
        // `!GPIO_RESET` is 1 and `!GPIO_SET` is 0 in C, which is what
        // `regulator_gpio.c:32` relies on to find the off level.
        assert_eq!(PinLevel::Reset.inverted(), PinLevel::Set);
        assert_eq!(PinLevel::Set.inverted(), PinLevel::Reset);
        for level in [PinLevel::Reset, PinLevel::Set] {
            assert_eq!(level.inverted().inverted(), level);
        }
    }

    #[test]
    fn the_discriminants_are_the_c_enum() {
        assert_eq!(PinLevel::Reset as u8, 0, "GPIO_RESET");
        assert_eq!(PinLevel::Set as u8, 1, "GPIO_SET");
    }

    #[test]
    fn levels_round_trip_through_the_embedded_hal_bool() {
        assert!(PinLevel::Set.is_high());
        assert!(!PinLevel::Reset.is_high());
        assert_eq!(PinLevel::from_high(true), PinLevel::Set);
        assert_eq!(PinLevel::from_high(false), PinLevel::Reset);
        for level in [PinLevel::Reset, PinLevel::Set] {
            assert_eq!(PinLevel::from_high(level.is_high()), level);
        }
    }

    #[test]
    fn a_pins_h_level_constant_is_read_the_way_c_reads_it() {
        // Both `BTN_LEVEL` and `I2C_INT_LEVEL` are 0 on both boards.
        assert_eq!(PinLevel::from_pins_h(0), PinLevel::Reset);
        assert_eq!(PinLevel::from_pins_h(1), PinLevel::Set);
        // C truthiness: anything non-zero is high.
        assert_eq!(PinLevel::from_pins_h(2), PinLevel::Set);
        assert_eq!(PinLevel::from_pins_h(-1), PinLevel::Set);
    }

    #[test]
    fn is_asserted_compares_against_the_active_level() {
        // Active low, as BTN/BTN_LEVEL and SD_CARD_nDET are.
        assert!(is_asserted(PinLevel::Reset, PinLevel::Reset));
        assert!(!is_asserted(PinLevel::Set, PinLevel::Reset));
        // Active high.
        assert!(is_asserted(PinLevel::Set, PinLevel::Set));
        assert!(!is_asserted(PinLevel::Reset, PinLevel::Set));
    }
}
