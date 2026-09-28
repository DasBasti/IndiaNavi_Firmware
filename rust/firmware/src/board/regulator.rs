//! Reference-counted power rails driven by a single GPIO.
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/hw/regulator.h,
//! lib/Platinenmacher_HAL_ESP32/hw/regulator_gpio.h,
//! lib/Platinenmacher_HAL_ESP32/hw/esp32/regulator_gpio.c
//!
//! All three switchable rails on this board are **active low** -- the pin names
//! say so (`GPS_VCC_nEN`, `EINK_VCC_nEN`, `SD_VCC_nEN`) and every C call site
//! spells it out by hand before creating the regulator:
//!
//! ```text
//! src/esp32/gps.c:158-162   reg_gpio->onValue = GPIO_RESET;  regulator_gpio_create(reg_gpio)
//! src/esp32/gui.c:358-360   reg_gpio->onValue = GPIO_RESET;  regulator_gpio_create(reg_gpio)
//! src/esp32/sd.c:260-262    reg_gpio->onValue = GPIO_RESET;  regulator_gpio_create(reg_gpio)
//! ```
//!
//! [`GpioRegulator::active_low`] is the constructor that bakes that in, so no
//! future call site can forget and drive a rail the wrong way round. The
//! polarity itself is still a parameter ([`GpioRegulator::new`]) because
//! `regulator_gpio.c` never hard-coded it.
//!
//! This module is pure: it is generic over `embedded_hal::digital::OutputPin`
//! and has no ESP-IDF dependency, so the refcounting and the polarity are
//! covered by the host tests at the bottom of the file.
//!
//! Note for whoever lands `pm-core`: the [`Regulator`] trait here is a
//! placeholder. `rust/PORTING.md` says the switchable rails belong behind
//! `pm_core::power::Regulator`, but `crates/pm-core/src/power.rs` is still a
//! three-line doc stub on the scaffold branch and `pm-core` is not on `main`
//! at all. When it lands, delete this trait and implement that one --
//! [`GpioRegulator`]'s inherent methods, which carry all the logic and all the
//! tests, do not change.

use embedded_hal::digital::{OutputPin, PinState};

use super::gpio::PinLevel;

/// Rail state.
///
/// Replaces `regulator_status_t` (`regulator.h:13-15`), discriminants pinned to
/// the C enum.
///
/// [`RegulatorStatus::Fault`] exists because `OFF, ON, FAULT` is what the C
/// enum listed; nothing in the C tree ever assigns it, and nothing here does
/// either. It is kept so the numbering matches while both trees coexist.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum RegulatorStatus {
    /// `OFF`
    Off = 0,
    /// `ON`
    On = 1,
    /// `FAULT` -- never produced, see the type docs.
    Fault = 2,
}

/// A switchable power rail.
///
/// Replaces the `enable`/`disable` function pointers of `regulator_t`
/// (`regulator.h:17-24`). The C struct was a hand-rolled vtable; this is the
/// trait it was imitating.
pub trait Regulator {
    /// How the underlying pin can fail.
    type Error;

    /// Take a reference on the rail, switching it on if it was off.
    fn enable(&mut self) -> Result<(), Self::Error>;
    /// Drop a reference, switching the rail off once the last one goes.
    fn disable(&mut self) -> Result<(), Self::Error>;
    /// Current state.
    fn status(&self) -> RegulatorStatus;
}

/// A rail switched by one GPIO, with the usage refcount of
/// `regulator_gpio.c`.
///
/// The refcount is what lets two independent tasks share a rail: the GPS task
/// and the map downloader both `enable()` the SD rail, and it only actually
/// drops when both have `disable()`d.
#[derive(Debug)]
pub struct GpioRegulator<P> {
    pin: P,
    /// The level that switches the rail **on** -- `gpio_t::onValue`
    /// (`gpio.h:47`).
    on_level: PinLevel,
    status: RegulatorStatus,
    /// `regulator_t::usage` (`regulator.h:22`), a `uint8_t` in C.
    usage: u8,
}

impl<P: OutputPin> GpioRegulator<P> {
    /// Create a rail and drive it to its off level.
    ///
    /// Replaces `regulator_gpio_create` (`regulator_gpio.c:38-46`), including
    /// its trailing `reg->disable(reg)`: `RTOS_Malloc` zeroes, so the C
    /// regulator started at `status = OFF`, `usage = 0`, and that first
    /// `disable` fell through to `gpio_write(gpio, !onValue)`. The rail is
    /// therefore actively driven off before this function returns, rather than
    /// left floating.
    pub fn new(pin: P, on_level: PinLevel) -> Result<Self, P::Error> {
        let mut reg = Self {
            pin,
            on_level,
            status: RegulatorStatus::Off,
            usage: 0,
        };
        reg.drive(on_level.inverted())?;
        Ok(reg)
    }

    /// Create an **active-low** rail: the pin is driven low to switch it on.
    ///
    /// This is the only polarity that occurs on either IndiaNavi board; see the
    /// module docs for the three C call sites it replaces.
    pub fn active_low(pin: P) -> Result<Self, P::Error> {
        Self::new(pin, PinLevel::Reset)
    }

    /// Create an active-high rail. No call site in the C tree uses one; it
    /// exists so the polarity stays a property of the rail and not of this
    /// module.
    pub fn active_high(pin: P) -> Result<Self, P::Error> {
        Self::new(pin, PinLevel::Set)
    }

    /// The level that switches this rail on, `gpio_t::onValue`.
    #[must_use]
    pub const fn on_level(&self) -> PinLevel {
        self.on_level
    }

    /// Outstanding references, `regulator_t::usage`.
    #[must_use]
    pub const fn usage(&self) -> u8 {
        self.usage
    }

    fn drive(&mut self, level: PinLevel) -> Result<(), P::Error> {
        self.pin.set_state(if level.is_high() {
            PinState::High
        } else {
            PinState::Low
        })
    }
}

impl<P: OutputPin> Regulator for GpioRegulator<P> {
    type Error = P::Error;

    /// Replaces `regulator_gpio_enable` (`regulator_gpio.c:12-22`).
    ///
    /// The count is bumped unconditionally and the pin is only touched on the
    /// off-to-on edge, exactly as in C.
    ///
    /// Deviation: C wrote `reg->usage++` on a `uint8_t`, which wraps to 0 at
    /// 256 nested enables and would then switch the rail off under the feet of
    /// 255 users. Rust would panic on that overflow in a debug build, so this
    /// saturates instead -- a 256th enable is a leak either way, but saturating
    /// keeps the rail on, which is the safe direction.
    ///
    /// The count is bumped *before* the pin is driven, so if the write fails the
    /// caller gets an `Err` while the reference has already been taken and
    /// `status` is still `Off`. That is deliberate: a retry then re-attempts the
    /// write instead of concluding the rail is up. C could not have this problem
    /// because `gpio_write` returned `void` and swallowed the failure.
    fn enable(&mut self) -> Result<(), Self::Error> {
        self.usage = self.usage.saturating_add(1);
        if self.status != RegulatorStatus::On {
            let on = self.on_level;
            self.drive(on)?;
            self.status = RegulatorStatus::On;
        }
        Ok(())
    }

    /// Replaces `regulator_gpio_disable` (`regulator_gpio.c:24-36`).
    ///
    /// The `if status != OFF` guard around the decrement is C's, and it is what
    /// makes a `disable()` on an already-off rail harmless instead of an
    /// underflow. Because the two `if`s are separate, a `disable()` that brings
    /// the count to zero is also the one that drops the rail, in the same call.
    fn disable(&mut self) -> Result<(), Self::Error> {
        if self.status != RegulatorStatus::Off {
            // Guarded by the `status` check above, so this cannot underflow in
            // practice; `saturating_sub` keeps that true by construction rather
            // than by argument.
            self.usage = self.usage.saturating_sub(1);
        }
        if self.usage == 0 {
            let off = self.on_level.inverted();
            self.drive(off)?;
            self.status = RegulatorStatus::Off;
        }
        Ok(())
    }

    fn status(&self) -> RegulatorStatus {
        self.status
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::mock::MockOutputPin;

    fn active_low() -> GpioRegulator<MockOutputPin> {
        GpioRegulator::active_low(MockOutputPin::new()).expect("mock pin never fails")
    }

    #[test]
    fn creation_drives_an_active_low_rail_off_meaning_high() {
        let reg = active_low();
        assert_eq!(reg.status(), RegulatorStatus::Off);
        assert_eq!(reg.usage(), 0);
        // regulator_gpio_create's trailing disable() -> gpio_write(!onValue).
        assert_eq!(reg.pin.writes(), &[PinState::High]);
        assert_eq!(reg.pin.state(), Some(PinState::High));
    }

    #[test]
    fn creation_drives_an_active_high_rail_off_meaning_low() {
        let reg = GpioRegulator::active_high(MockOutputPin::new()).unwrap();
        assert_eq!(reg.pin.writes(), &[PinState::Low]);
    }

    #[test]
    fn enable_pulls_an_active_low_rail_low() {
        let mut reg = active_low();
        reg.enable().unwrap();
        assert_eq!(reg.status(), RegulatorStatus::On);
        assert_eq!(reg.usage(), 1);
        assert_eq!(reg.pin.writes(), &[PinState::High, PinState::Low]);
    }

    #[test]
    fn enable_is_idempotent_on_the_pin_but_not_on_the_count() {
        let mut reg = active_low();
        reg.enable().unwrap();
        reg.enable().unwrap();
        assert_eq!(reg.usage(), 2);
        // Only one falling edge: the pin is untouched while already ON.
        assert_eq!(reg.pin.writes(), &[PinState::High, PinState::Low]);
    }

    #[test]
    fn the_rail_survives_until_the_last_user_releases_it() {
        let mut reg = active_low();
        reg.enable().unwrap();
        reg.enable().unwrap();

        reg.disable().unwrap();
        assert_eq!(reg.usage(), 1);
        assert_eq!(reg.status(), RegulatorStatus::On, "still one user left");
        assert_eq!(reg.pin.state(), Some(PinState::Low), "rail still on");

        reg.disable().unwrap();
        assert_eq!(reg.usage(), 0);
        assert_eq!(reg.status(), RegulatorStatus::Off);
        assert_eq!(
            reg.pin.writes(),
            &[PinState::High, PinState::Low, PinState::High]
        );
    }

    #[test]
    fn disabling_an_already_off_rail_does_not_underflow() {
        let mut reg = active_low();
        for _ in 0..5 {
            reg.disable().unwrap();
        }
        assert_eq!(reg.usage(), 0);
        assert_eq!(reg.status(), RegulatorStatus::Off);
        // Each call re-asserts the off level, as `regulator_gpio.c` does.
        assert_eq!(reg.pin.writes().len(), 1 + 5);
        assert!(reg.pin.writes().iter().all(|s| *s == PinState::High));
    }

    #[test]
    fn a_full_off_on_off_cycle_ends_where_it_started() {
        let mut reg = active_low();
        reg.enable().unwrap();
        reg.disable().unwrap();
        assert_eq!(reg.status(), RegulatorStatus::Off);
        assert_eq!(reg.pin.state(), Some(PinState::High));
        // And it can be brought back up.
        reg.enable().unwrap();
        assert_eq!(reg.pin.state(), Some(PinState::Low));
    }

    #[test]
    fn the_usage_count_saturates_instead_of_wrapping_the_rail_off() {
        let mut reg = active_low();
        for _ in 0..300 {
            reg.enable().unwrap();
        }
        assert_eq!(reg.usage(), u8::MAX);
        assert_eq!(reg.status(), RegulatorStatus::On);
        assert_eq!(reg.pin.state(), Some(PinState::Low), "rail stayed on");
    }

    #[test]
    fn on_level_is_reported_back() {
        assert_eq!(active_low().on_level(), PinLevel::Reset);
        assert_eq!(
            GpioRegulator::active_high(MockOutputPin::new())
                .unwrap()
                .on_level(),
            PinLevel::Set
        );
    }

    #[test]
    fn a_failing_pin_propagates_instead_of_panicking() {
        let mut reg =
            GpioRegulator::new(MockOutputPin::new(), PinLevel::Reset).expect("create succeeds");
        reg.pin.fail_next();
        assert!(reg.enable().is_err());
    }
}
