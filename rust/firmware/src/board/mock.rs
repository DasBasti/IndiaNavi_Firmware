//! Recording `embedded-hal` pin fakes, for the host tests in this module only.
//!
//! No C counterpart: the C tree had no host tests for the HAL. Modelled on
//! `rust/crates/acep-5in65-7c/src/mock.rs`, which does the same thing for
//! `SpiDevice`.
//!
//! These record every write in order rather than only the final state, because
//! the thing worth asserting about a power rail is the *sequence* of edges -- a
//! regulator that ends up off having glitched on in between is a bug the final
//! state cannot see.

use embedded_hal::digital::{Error, ErrorKind, ErrorType, InputPin, OutputPin, PinState};

/// An error a fake pin can be told to return, so the `Result` paths get
/// exercised instead of being assumed infallible.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MockPinError;

impl Error for MockPinError {
    fn kind(&self) -> ErrorKind {
        ErrorKind::Other
    }
}

/// An `OutputPin` that remembers everything written to it.
#[derive(Debug, Default)]
pub struct MockOutputPin {
    writes: Vec<PinState>,
    fail_next: bool,
}

impl MockOutputPin {
    /// A pin that has never been written, so its state is unknown -- the
    /// situation a real GPIO is in before `gpio_config`.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Every write, oldest first.
    #[must_use]
    pub fn writes(&self) -> &[PinState] {
        &self.writes
    }

    /// The most recent write, or `None` if the pin was never driven.
    #[must_use]
    pub fn state(&self) -> Option<PinState> {
        self.writes.last().copied()
    }

    /// Make the next write fail once.
    pub fn fail_next(&mut self) {
        self.fail_next = true;
    }
}

impl ErrorType for MockOutputPin {
    type Error = MockPinError;
}

impl OutputPin for MockOutputPin {
    fn set_low(&mut self) -> Result<(), Self::Error> {
        self.set_state(PinState::Low)
    }

    fn set_high(&mut self) -> Result<(), Self::Error> {
        self.set_state(PinState::High)
    }

    fn set_state(&mut self, state: PinState) -> Result<(), Self::Error> {
        if core::mem::replace(&mut self.fail_next, false) {
            return Err(MockPinError);
        }
        self.writes.push(state);
        Ok(())
    }
}

/// An `InputPin` that replays a fixed sequence of levels.
///
/// Once the sequence runs out the last value repeats, so a test only has to
/// list the interesting readings.
#[derive(Debug)]
pub struct MockInputPin {
    levels: Vec<bool>,
    next: usize,
    fail_next: bool,
}

impl MockInputPin {
    /// A pin that always reads high.
    #[must_use]
    pub fn high() -> Self {
        Self::sequence([true])
    }

    /// A pin that always reads low.
    #[must_use]
    pub fn low() -> Self {
        Self::sequence([false])
    }

    /// A pin that reads the given levels in order, then holds the last one.
    ///
    /// # Panics
    ///
    /// If `levels` is empty. This is a test helper, and an empty sequence is a
    /// mistake in the test rather than an input to handle.
    #[must_use]
    pub fn sequence<I: IntoIterator<Item = bool>>(levels: I) -> Self {
        let levels: Vec<bool> = levels.into_iter().collect();
        assert!(!levels.is_empty(), "a mock input pin needs a level to read");
        Self {
            levels,
            next: 0,
            fail_next: false,
        }
    }

    /// Make the next read fail once.
    pub fn fail_next(&mut self) {
        self.fail_next = true;
    }

    fn advance(&mut self) -> Result<bool, MockPinError> {
        if core::mem::replace(&mut self.fail_next, false) {
            return Err(MockPinError);
        }
        let index = self.next.min(self.levels.len() - 1);
        self.next = index + 1;
        Ok(self.levels[index])
    }
}

impl ErrorType for MockInputPin {
    type Error = MockPinError;
}

impl InputPin for MockInputPin {
    fn is_high(&mut self) -> Result<bool, Self::Error> {
        self.advance()
    }

    fn is_low(&mut self) -> Result<bool, Self::Error> {
        Ok(!self.advance()?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_output_pin_records_every_write_in_order() {
        let mut pin = MockOutputPin::new();
        assert_eq!(pin.state(), None, "never driven");
        pin.set_high().unwrap();
        pin.set_low().unwrap();
        pin.set_low().unwrap();
        assert_eq!(
            pin.writes(),
            &[PinState::High, PinState::Low, PinState::Low],
            "repeats are kept: a redundant write is still an event"
        );
        assert_eq!(pin.state(), Some(PinState::Low));
    }

    #[test]
    fn an_output_pin_fails_once_when_told_to() {
        let mut pin = MockOutputPin::new();
        pin.fail_next();
        assert!(pin.set_high().is_err());
        assert!(pin.set_high().is_ok());
        assert_eq!(
            pin.writes(),
            &[PinState::High],
            "the failed write was not recorded"
        );
    }

    #[test]
    fn an_input_pin_replays_then_holds_the_last_level() {
        let mut pin = MockInputPin::sequence([false, true]);
        assert!(!pin.is_high().unwrap());
        assert!(pin.is_high().unwrap());
        assert!(pin.is_high().unwrap(), "held");
        assert!(pin.is_high().unwrap(), "still held");
    }

    #[test]
    fn is_low_is_the_inverse_of_is_high() {
        let mut pin = MockInputPin::low();
        assert!(pin.is_low().unwrap());
        let mut pin = MockInputPin::high();
        assert!(!pin.is_low().unwrap());
    }

    #[test]
    fn an_input_pin_fails_once_when_told_to() {
        let mut pin = MockInputPin::high();
        pin.fail_next();
        assert!(pin.is_high().is_err());
        assert!(pin.is_high().unwrap());
    }

    #[test]
    #[should_panic(expected = "needs a level to read")]
    fn an_empty_sequence_is_a_test_bug() {
        let _ = MockInputPin::sequence([]);
    }
}
