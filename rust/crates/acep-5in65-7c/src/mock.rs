//! Recording fakes for the `embedded-hal` traits, used by the driver tests.
//!
//! No C counterpart: the C driver talked to `spi_master.h` directly and could
//! only be exercised on hardware. `test/host/Platinenmacher/mock/` has fakes
//! for the *display* layer, not for the panel, so there is nothing to port.
//!
//! Every MOSI byte is recorded together with the level the D/C line was at
//! when it went out, which is what makes "did we send the right init
//! sequence?" a host-testable question.

use alloc::rc::Rc;
use alloc::vec::Vec;
use core::cell::RefCell;
use core::convert::Infallible;

use embedded_hal::delay::DelayNs;
use embedded_hal::digital::{ErrorType as DigitalErrorType, InputPin, OutputPin};
use embedded_hal::spi::{ErrorType as SpiErrorType, Operation, SpiDevice};

use crate::driver::{Acep5In65, Rotation};

/// One SPI transfer, tagged with the D/C level it went out under.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Transfer {
    /// `false` = command (D/C low), `true` = data (D/C high).
    pub data: bool,
    /// The bytes on MOSI.
    pub bytes: Vec<u8>,
}

/// Everything the fakes observed, in order.
#[derive(Debug, Default)]
pub struct Recorder {
    /// SPI transfers, oldest first.
    pub transfers: Vec<Transfer>,
    /// Current D/C level; `true` is data.
    pub dc: bool,
    /// Every level the power-enable pin was driven to.
    pub power: Vec<bool>,
    /// Every delay requested, in milliseconds.
    pub delays_ms: Vec<u32>,
}

impl Recorder {
    /// Total number of bytes clocked out on MOSI.
    pub fn byte_count(&self) -> usize {
        self.transfers.iter().map(|t| t.bytes.len()).sum()
    }

    /// The transfers flattened to one `(is_data, byte)` pair per byte, so a
    /// test can compare against a literal sequence regardless of how the
    /// driver chose to chunk its writes.
    pub fn flat(&self) -> Vec<(bool, u8)> {
        let mut out = Vec::with_capacity(self.byte_count());
        for t in &self.transfers {
            for b in &t.bytes {
                out.push((t.data, *b));
            }
        }
        out
    }
}

/// Shared handle on a [`Recorder`].
pub type Shared = Rc<RefCell<Recorder>>;

/// Fake `SpiDevice` that appends to the shared [`Recorder`].
pub struct MockSpi(Shared);

impl SpiErrorType for MockSpi {
    type Error = Infallible;
}

impl SpiDevice<u8> for MockSpi {
    fn transaction(&mut self, operations: &mut [Operation<'_, u8>]) -> Result<(), Infallible> {
        for op in operations {
            if let Operation::Write(buf) = op {
                let data = self.0.borrow().dc;
                self.0.borrow_mut().transfers.push(Transfer {
                    data,
                    bytes: buf.to_vec(),
                });
            }
        }
        Ok(())
    }
}

/// Fake D/C pin: updates the level the recorder tags transfers with.
pub struct MockDc(Shared);

impl DigitalErrorType for MockDc {
    type Error = Infallible;
}

impl OutputPin for MockDc {
    fn set_low(&mut self) -> Result<(), Infallible> {
        self.0.borrow_mut().dc = false;
        Ok(())
    }
    fn set_high(&mut self) -> Result<(), Infallible> {
        self.0.borrow_mut().dc = true;
        Ok(())
    }
}

/// Fake regulator-enable pin; records every level it is driven to.
pub struct MockPower(Shared);

impl DigitalErrorType for MockPower {
    type Error = Infallible;
}

impl OutputPin for MockPower {
    fn set_low(&mut self) -> Result<(), Infallible> {
        self.0.borrow_mut().power.push(false);
        Ok(())
    }
    fn set_high(&mut self) -> Result<(), Infallible> {
        self.0.borrow_mut().power.push(true);
        Ok(())
    }
}

/// Fake BUSY pin that replays a scripted sequence of levels and then repeats
/// `tail` forever.
pub struct MockBusy {
    script: Vec<bool>,
    at: usize,
    tail: bool,
}

impl DigitalErrorType for MockBusy {
    type Error = Infallible;
}

impl InputPin for MockBusy {
    fn is_high(&mut self) -> Result<bool, Infallible> {
        let level = match self.script.get(self.at) {
            Some(l) => *l,
            None => self.tail,
        };
        self.at += 1;
        Ok(level)
    }
    fn is_low(&mut self) -> Result<bool, Infallible> {
        Ok(!self.is_high()?)
    }
}

/// Fake delay that only records how long it was asked to wait.
pub struct MockDelay(Shared);

impl DelayNs for MockDelay {
    fn delay_ns(&mut self, ns: u32) {
        self.0.borrow_mut().delays_ms.push(ns / 1_000_000);
    }
    fn delay_ms(&mut self, ms: u32) {
        self.0.borrow_mut().delays_ms.push(ms);
    }
}

/// A driver wired to the fakes, plus the handle to inspect what it did.
pub type MockDriver = Acep5In65<MockSpi, MockDc, MockPower, MockBusy, MockDelay>;

/// Build a driver whose BUSY pin replays `busy_script` and then holds
/// `busy_tail`.
pub fn driver_with(
    rotation: Rotation,
    busy_script: &[bool],
    busy_tail: bool,
) -> (MockDriver, Shared) {
    let shared: Shared = Rc::new(RefCell::new(Recorder::default()));
    let driver = Acep5In65::new(
        MockSpi(Rc::clone(&shared)),
        MockDc(Rc::clone(&shared)),
        MockPower(Rc::clone(&shared)),
        MockBusy {
            script: busy_script.to_vec(),
            at: 0,
            tail: busy_tail,
        },
        MockDelay(Rc::clone(&shared)),
        rotation,
    );
    (driver, shared)
}

/// Build a driver whose BUSY pin is high whenever it is sampled.
pub fn driver(rotation: Rotation) -> (MockDriver, Shared) {
    driver_with(rotation, &[], true)
}
