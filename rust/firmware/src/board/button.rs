//! The single user button.
//!
//! Replaces: the `BTN` / `BTN_LEVEL` handling in src/esp32/main.c --
//! `handleButtonPress` (:179-193), `button_timer_trigger` (:198-205), the
//! `esp_btn` `gpio_config_t` (:247-252), the wake-up check (:253-259), the ISR
//! registration (:280-282), the long-press timer (:300-303) and the
//! `TASK_EVENT_BUTTON_*` dispatch (:358-367)
//!
//! `BTN_LEVEL` is 0 on both boards: the button pulls the line **low** when
//! pressed, and the internal pull-up holds it high otherwise. The C code
//! compared against `BTN_LEVEL` in the ISR but then wrote `gpio_get_level(BTN)`
//! bare in the wake-up path (`main.c:255`, "if the pin is high, the press was
//! too short"), which happens to agree only because the active level is 0.
//! [`Button::is_pressed`] is the one place that knows.
//!
//! Everything in this module except [`Button::new`] is pure, and the press/
//! release decoding is covered by the host tests at the bottom.

use embedded_hal::digital::InputPin;

use super::gpio::{is_asserted, PinLevel};
use super::pins::PINS;

/// Level the button reads when pressed, from `BTN_LEVEL`.
pub const ACTIVE_LEVEL: PinLevel = PinLevel::from_pins_h(PINS.btn_level);

/// How long the button must be held for a long press, in milliseconds.
///
/// src/esp32/main.c:361 arms a one-shot `esp_timer` for 3 000 000 us.
pub const LONG_PRESS_MS: u32 = 3_000;

/// How long the firmware waits after an EXT0 wake-up before deciding the press
/// was real, in milliseconds.
///
/// src/esp32/main.c:254: `vTaskDelay(pdMS_TO_TICKS(2000))`, then the button is
/// re-read. If it has been released by then the device goes back to sleep
/// (:255-258).
pub const WAKEUP_HOLD_MS: u32 = 2_000;

/// A button edge.
///
/// Replaces the `TASK_EVENT_BUTTON_DOWN` / `TASK_EVENT_BUTTON_UP` values
/// `handleButtonPress` pushed onto `eventQueueHandle` (src/esp32/main.c:187-190).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ButtonEvent {
    /// Pressed: the line read [`ACTIVE_LEVEL`].
    Down,
    /// Released.
    Up,
}

/// Decode one reading of the button line into the event the C ISR would have
/// queued.
///
/// Replaces src/esp32/main.c:186-191 verbatim: the ISR fires on any edge and
/// classifies by the level it then reads, so a press shorter than the interrupt
/// latency is reported as `Up` -- the same lost-edge behaviour as C.
#[must_use]
pub const fn event_for(level: PinLevel) -> ButtonEvent {
    if is_asserted(level, ACTIVE_LEVEL) {
        ButtonEvent::Down
    } else {
        ButtonEvent::Up
    }
}

/// The button, over any `embedded-hal` input pin.
///
/// Generic rather than concrete so the decoding above is testable without a
/// device; the firmware instantiates it with
/// [`super::gpio::input_pullup`]'s `PinDriver`, which is where the pull-up and
/// the any-edge interrupt of `main.c:249-251, :280` get configured.
#[derive(Debug)]
pub struct Button<P> {
    pin: P,
}

impl<P: InputPin> Button<P> {
    /// Wrap a configured input pin.
    ///
    /// The pin must already have its pull-up on: the board only has the
    /// internal one, so a floating `BTN` reads as a permanent press.
    pub const fn new(pin: P) -> Self {
        Self { pin }
    }

    /// Read the raw line level.
    pub fn level(&mut self) -> Result<PinLevel, P::Error> {
        Ok(PinLevel::from_high(self.pin.is_high()?))
    }

    /// Is the button down right now?
    ///
    /// Replaces `gpio_get_level(BTN) == BTN_LEVEL` (src/esp32/main.c:186) and
    /// the inverted spelling of the same test at :255.
    pub fn is_pressed(&mut self) -> Result<bool, P::Error> {
        Ok(is_asserted(self.level()?, ACTIVE_LEVEL))
    }

    /// The event the C ISR would have queued for the current level.
    pub fn event(&mut self) -> Result<ButtonEvent, P::Error> {
        Ok(event_for(self.level()?))
    }

    /// Give the pin back.
    pub fn release(self) -> P {
        self.pin
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::mock::MockInputPin;
    use crate::board::pins::{ESP32S3, ESP32_REV2};

    #[test]
    fn the_button_is_active_low_on_both_boards() {
        assert_eq!(PinLevel::from_pins_h(ESP32S3.btn_level), PinLevel::Reset);
        assert_eq!(PinLevel::from_pins_h(ESP32_REV2.btn_level), PinLevel::Reset);
        assert_eq!(ACTIVE_LEVEL, PinLevel::Reset);
    }

    #[test]
    fn a_low_line_is_a_press() {
        assert_eq!(event_for(PinLevel::Reset), ButtonEvent::Down);
        assert_eq!(event_for(PinLevel::Set), ButtonEvent::Up);
    }

    #[test]
    fn the_pin_wrapper_agrees_with_the_pure_decoder() {
        let mut button = Button::new(MockInputPin::low());
        assert_eq!(button.level().unwrap(), PinLevel::Reset);
        assert!(button.is_pressed().unwrap());
        assert_eq!(button.event().unwrap(), ButtonEvent::Down);

        let mut button = Button::new(MockInputPin::high());
        assert_eq!(button.level().unwrap(), PinLevel::Set);
        assert!(!button.is_pressed().unwrap());
        assert_eq!(button.event().unwrap(), ButtonEvent::Up);
    }

    #[test]
    fn a_press_and_release_sequence_decodes_in_order() {
        let mut button = Button::new(MockInputPin::sequence([false, false, true]));
        assert_eq!(button.event().unwrap(), ButtonEvent::Down);
        assert_eq!(button.event().unwrap(), ButtonEvent::Down);
        assert_eq!(button.event().unwrap(), ButtonEvent::Up);
    }

    #[test]
    fn a_failing_pin_propagates_instead_of_reporting_a_press() {
        let mut pin = MockInputPin::high();
        pin.fail_next();
        let mut button = Button::new(pin);
        assert!(button.is_pressed().is_err());
    }

    #[test]
    fn timings_match_main_c() {
        assert_eq!(LONG_PRESS_MS, 3_000, "src/esp32/main.c:361");
        assert_eq!(WAKEUP_HOLD_MS, 2_000, "src/esp32/main.c:254");
    }
}
