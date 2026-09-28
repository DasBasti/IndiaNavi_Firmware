//! The single status LED.
//!
//! Replaces: the `led` global and `light_sleep_cb` in src/esp32/main.c:78,
//! :90-104, :240, and the `ledDelay` at :72
//!
//! The LED is active **high**: `esp_pm_config` hands `light_sleep_cb` a
//! `GPIO_RESET` on the way into light sleep and a `GPIO_SET` on the way out
//! (src/esp32/main.c:101-104), so lit means awake. That is the only thing the C
//! tree ever does with it -- there is no blink pattern, and `ledDelay` is not a
//! LED timer at all despite the name: it is the `xQueueReceive` timeout of the
//! main event loop (:358).
//!
//! Generic over `embedded_hal::digital::OutputPin`, so the polarity is covered
//! by the host tests below.

use embedded_hal::digital::{OutputPin, PinState};

use super::gpio::PinLevel;

/// Level that lights the LED.
///
/// `light_sleep_cb` is passed `GPIO_SET` on sleep *exit*
/// (src/esp32/main.c:103), so high is on.
pub const ON_LEVEL: PinLevel = PinLevel::Set;

/// Timeout of the main event loop's `xQueueReceive`, in milliseconds.
///
/// src/esp32/main.c:72 calls it `ledDelay` and :358 uses it as the queue
/// timeout. Kept here because the name says LED and the next reader will look
/// here for it; it does not drive the LED.
pub const EVENT_LOOP_TIMEOUT_MS: u32 = 100;

/// The status LED, over any `embedded-hal` output pin.
#[derive(Debug)]
pub struct Led<P> {
    pin: P,
}

impl<P: OutputPin> Led<P> {
    /// Wrap an output pin and switch the LED on.
    ///
    /// Replaces `led = gpio_create(OUTPUT, 0, LED)` (src/esp32/main.c:240).
    /// `gpio_create` left the level alone, so the C LED sat at whatever the pin
    /// reset state was until the first light-sleep transition; starting it lit
    /// matches what that transition would have made it -- the device is awake.
    pub fn new(pin: P) -> Result<Self, P::Error> {
        let mut led = Self { pin };
        led.on()?;
        Ok(led)
    }

    /// Drive the LED to a raw level.
    ///
    /// Replaces `gpio_write(led, level)` in `light_sleep_cb`
    /// (src/esp32/main.c:95), which is handed the level directly rather than an
    /// on/off.
    pub fn set_level(&mut self, level: PinLevel) -> Result<(), P::Error> {
        self.pin.set_state(if level.is_high() {
            PinState::High
        } else {
            PinState::Low
        })
    }

    /// Light it.
    pub fn on(&mut self) -> Result<(), P::Error> {
        self.set_level(ON_LEVEL)
    }

    /// Extinguish it.
    pub fn off(&mut self) -> Result<(), P::Error> {
        self.set_level(ON_LEVEL.inverted())
    }

    /// What `light_sleep_cb` did on the way *into* light sleep
    /// (src/esp32/main.c:102 passes `GPIO_RESET` as `enter_cb_user_arg`).
    pub fn entering_light_sleep(&mut self) -> Result<(), P::Error> {
        self.off()
    }

    /// What `light_sleep_cb` did on the way *out* of light sleep
    /// (src/esp32/main.c:103 passes `GPIO_SET` as `exit_cb_user_arg`).
    pub fn leaving_light_sleep(&mut self) -> Result<(), P::Error> {
        self.on()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::mock::MockOutputPin;

    #[test]
    fn the_led_starts_lit() {
        let led = Led::new(MockOutputPin::new()).unwrap();
        assert_eq!(led.pin.writes(), &[PinState::High]);
    }

    #[test]
    fn on_is_high_and_off_is_low() {
        let mut led = Led::new(MockOutputPin::new()).unwrap();
        led.off().unwrap();
        assert_eq!(led.pin.state(), Some(PinState::Low));
        led.on().unwrap();
        assert_eq!(led.pin.state(), Some(PinState::High));
    }

    #[test]
    fn light_sleep_transitions_match_the_pm_callbacks() {
        let mut led = Led::new(MockOutputPin::new()).unwrap();
        led.entering_light_sleep().unwrap();
        assert_eq!(led.pin.state(), Some(PinState::Low), "dark while asleep");
        led.leaving_light_sleep().unwrap();
        assert_eq!(led.pin.state(), Some(PinState::High), "lit while awake");

        assert_eq!(
            led.pin.writes(),
            &[PinState::High, PinState::Low, PinState::High]
        );
    }

    #[test]
    fn a_raw_level_is_passed_through() {
        let mut led = Led::new(MockOutputPin::new()).unwrap();
        led.set_level(PinLevel::Reset).unwrap();
        assert_eq!(led.pin.state(), Some(PinState::Low));
        led.set_level(PinLevel::Set).unwrap();
        assert_eq!(led.pin.state(), Some(PinState::High));
    }

    #[test]
    fn a_failing_pin_propagates() {
        let mut pin = MockOutputPin::new();
        pin.fail_next();
        assert!(Led::new(pin).is_err());
    }

    #[test]
    fn the_event_loop_timeout_matches_main_c() {
        assert_eq!(EVENT_LOOP_TIMEOUT_MS, 100, "src/esp32/main.c:72");
    }
}
