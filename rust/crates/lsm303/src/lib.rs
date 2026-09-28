//! LSM303AH accelerometer + magnetometer driver.
//!
//! Replaces `lib/lsm303/lsm303.c` and `lib/lsm303/lsm303.h`.
//!
//! The C driver talked to the ESP-IDF `driver/i2c.h` command-link API directly
//! and owned the bus configuration. This port is generic over an
//! [`embedded_hal::i2c::I2c`] implementation and owns nothing but the bus
//! handle, so it builds and unit-tests on the host.
//!
//! # Wire compatibility
//!
//! Register addresses, the control-register bit layouts and the write
//! sequences are byte-identical to the C driver:
//!
//! * [`Lsm303::init`] reads `WHO_AM_I_A` and checks it against `0x43`, like
//!   `lsm303_init()` did after configuring the bus.
//! * [`Lsm303::enable_tap_detection`] writes the same four registers in the
//!   same order as `lsm303_enable_taping()`.
//! * Register reads are a `write(reg)` transaction followed by a separate
//!   `read(..)` transaction, matching `i2c_master_write_slave()` +
//!   `i2c_master_read_slave()`. Multi-byte reads rely on the device's
//!   `IF_ADD_INC` auto-increment, which is enabled by default, exactly as the
//!   C driver did.
//!
//! The data reads, the magnetometer configuration and the heading maths have no
//! counterpart in the C driver (`lsm303_mag_write_cfg()` was declared in
//! `lsm303.h` but never implemented, and nothing ever read the output
//! registers); they follow the LSM303AH datasheet.
//!
//! # Example
//!
//! ```no_run
//! # fn example<I: embedded_hal::i2c::I2c>(i2c: I) -> Result<(), lsm303::Error<I::Error>> {
//! let mut sensor = lsm303::Lsm303::new(i2c);
//! sensor.init()?;
//! sensor.configure_magnetometer(lsm303::MagConfig::default())?;
//! let acc = sensor.read_acceleration()?;
//! let mag = sensor.read_magnetic_field()?;
//! let heading = lsm303::heading::tilt_compensated_heading_degrees(&acc, &mag);
//! # let _ = heading;
//! # Ok(())
//! # }
//! ```

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

use embedded_hal::i2c::I2c;

mod float;

pub mod config;
pub mod heading;
pub mod register;

pub use config::{
    AccCtrl1, AccCtrl2, AccCtrl3, AccCtrl4, AccCtrl5, MagCfgA, MagCfgB, MagCfgC, MagInt,
    MagIntSource, MagStatus,
};
pub use heading::{heading_degrees, tilt, tilt_compensated_heading_degrees, Tilt};

/// Errors this driver can report.
///
/// The C driver returned `esp_err_t` and used `ESP_ERROR_CHECK` (i.e. abort) on
/// every bus failure. This port propagates the bus error instead; `E` is the
/// underlying [`embedded_hal::i2c::ErrorType::Error`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error<E> {
    /// The I2C transfer failed.
    Bus(E),
    /// `WHO_AM_I` did not hold the expected value.
    ///
    /// `lsm303_init()` logged "WHO AM I register does not match!" and returned
    /// `ESP_ERR_INVALID_ARG` here.
    WrongDevice {
        /// Value the register should have held.
        expected: u8,
        /// Value that was actually read.
        found: u8,
    },
    /// A register write was longer than the driver scratch buffer allows.
    ///
    /// The largest write the device needs is the six hard-iron offset bytes.
    WriteTooLong,
}

impl<E> From<E> for Error<E> {
    fn from(e: E) -> Self {
        Error::Bus(e)
    }
}

/// A raw three-axis accelerometer sample, in LSB of the configured full scale.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Acceleration {
    /// X axis.
    pub x: i16,
    /// Y axis.
    pub y: i16,
    /// Z axis.
    pub z: i16,
}

/// A raw three-axis magnetometer sample, in LSB (1.5 mgauss per LSB).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MagneticField {
    /// X axis.
    pub x: i16,
    /// Y axis.
    pub y: i16,
    /// Z axis.
    pub z: i16,
}

/// Magnetometer configuration written by [`Lsm303::configure_magnetometer`].
///
/// `lsm303_mag_write_cfg()` was declared in `lsm303.h` but never implemented,
/// so these defaults come from the datasheet rather than from the C driver:
/// continuous mode at 10 Hz with temperature compensation, offset cancellation
/// and block data update on, which is what a compass read-out wants.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MagConfig {
    /// `CFG_REG_A_M` content.
    pub cfg_a: MagCfgA,
    /// `CFG_REG_B_M` content.
    pub cfg_b: MagCfgB,
    /// `CFG_REG_C_M` content.
    pub cfg_c: MagCfgC,
}

impl Default for MagConfig {
    fn default() -> Self {
        Self {
            cfg_a: MagCfgA {
                comp_temp_en: true,
                reboot: false,
                soft_rst: false,
                lp: false,
                odr: 0, // 10 Hz
                md: 0,  // continuous
            },
            cfg_b: MagCfgB {
                off_canc: true,
                ..MagCfgB::default()
            },
            cfg_c: MagCfgC {
                bdu: true,
                ..MagCfgC::default()
            },
        }
    }
}

/// Control-register values `lsm303_enable_taping()` wrote.
///
/// Exposed so callers and tests can reason about the sequence without
/// rebuilding the bit layouts.
pub mod tap_defaults {
    use crate::config::{AccCtrl1, AccCtrl3};

    /// `CTRL1_A`: `{ .odr = 7, .fs = 3 }`, which the C bitfield packs as 0x1F.
    pub const CTRL1: AccCtrl1 = AccCtrl1 {
        odr: 7,
        fs: 3,
        hf_odr: false,
        bdu: false,
    };
    /// `CTRL3_A`: tap recognition on all three axes, packed as 0x1C.
    pub const CTRL3: AccCtrl3 = AccCtrl3 {
        st: 0,
        tap_x_en: true,
        tap_y_en: true,
        tap_z_en: true,
        lir: false,
        h_lactive: false,
        pp_od: false,
    };
    /// `TAP_6D_THS_A` threshold.
    pub const TAP_THRESHOLD: u8 = 1;
    /// `WAKE_UP_THS_A` content: bit 7 (`SINGLE_DOUBLE_TAP`) set, threshold 0.
    pub const WAKE_UP_THRESHOLD: u8 = 0x80;
}

/// LSM303AH driver over a shared I2C bus.
///
/// The accelerometer and the magnetometer are two dies behind two addresses
/// ([`register::ACC_ADDR`] and [`register::MAG_ADDR`]) on the same bus.
#[derive(Debug)]
pub struct Lsm303<I2C> {
    i2c: I2C,
}

impl<I2C, E> Lsm303<I2C>
where
    I2C: I2c<Error = E>,
{
    /// Wrap an I2C bus. Performs no transfer; call [`Lsm303::init`] next.
    pub fn new(i2c: I2C) -> Self {
        Self { i2c }
    }

    /// Give the I2C bus back.
    pub fn release(self) -> I2C {
        self.i2c
    }

    // -- raw register access (was lsm303_{acc,mag}_register_{read,write}) --

    /// Read `data.len()` bytes starting at `reg` from the accelerometer die.
    pub fn acc_register_read(&mut self, reg: u8, data: &mut [u8]) -> Result<(), Error<E>> {
        self.register_read(register::ACC_ADDR, reg, data)
    }

    /// Write `data` starting at `reg` on the accelerometer die.
    pub fn acc_register_write(&mut self, reg: u8, data: &[u8]) -> Result<(), Error<E>> {
        self.register_write(register::ACC_ADDR, reg, data)
    }

    /// Write a single byte to `reg` on the accelerometer die.
    pub fn acc_register_write_byte(&mut self, reg: u8, data: u8) -> Result<(), Error<E>> {
        self.acc_register_write(reg, &[data])
    }

    /// Read `data.len()` bytes starting at `reg` from the magnetometer die.
    pub fn mag_register_read(&mut self, reg: u8, data: &mut [u8]) -> Result<(), Error<E>> {
        self.register_read(register::MAG_ADDR, reg, data)
    }

    /// Write `data` starting at `reg` on the magnetometer die.
    pub fn mag_register_write(&mut self, reg: u8, data: &[u8]) -> Result<(), Error<E>> {
        self.register_write(register::MAG_ADDR, reg, data)
    }

    /// Write a single byte to `reg` on the magnetometer die.
    pub fn mag_register_write_byte(&mut self, reg: u8, data: u8) -> Result<(), Error<E>> {
        self.mag_register_write(reg, &[data])
    }

    /// Address the register, then read it back in a second transaction.
    ///
    /// The C driver stopped the bus between the two phases rather than issuing
    /// a repeated start, so this does the same.
    fn register_read(&mut self, addr: u8, reg: u8, data: &mut [u8]) -> Result<(), Error<E>> {
        self.i2c.write(addr, &[reg])?;
        if data.is_empty() {
            return Ok(());
        }
        self.i2c.read(addr, data)?;
        Ok(())
    }

    fn register_write(&mut self, addr: u8, reg: u8, data: &[u8]) -> Result<(), Error<E>> {
        // Up to six offset/config bytes plus the register address; the C driver
        // never wrote more in one go.
        let mut buf = [0u8; 7];
        let len = 1 + data.len();
        if len > buf.len() {
            return Err(Error::WriteTooLong);
        }
        buf[0] = reg;
        buf[1..len].copy_from_slice(data);
        self.i2c.write(addr, &buf[..len])?;
        Ok(())
    }

    // -- device identification and setup --------------------------------

    /// Read `WHO_AM_I_A`.
    pub fn who_am_i(&mut self) -> Result<u8, Error<E>> {
        let mut data = [0u8; 1];
        self.acc_register_read(register::WHO_AM_I_A, &mut data)?;
        Ok(data[0])
    }

    /// Read `WHO_AM_I_M`.
    pub fn mag_who_am_i(&mut self) -> Result<u8, Error<E>> {
        let mut data = [0u8; 1];
        self.mag_register_read(register::WHO_AM_I_M, &mut data)?;
        Ok(data[0])
    }

    /// Verify the accelerometer is present, as `lsm303_init()` did.
    ///
    /// Bus setup is the caller's job now, so this only performs the
    /// `WHO_AM_I_A` check that followed it in C.
    pub fn init(&mut self) -> Result<(), Error<E>> {
        let found = self.who_am_i()?;
        if found != register::WHO_AM_I_A_VALUE {
            return Err(Error::WrongDevice {
                expected: register::WHO_AM_I_A_VALUE,
                found,
            });
        }
        Ok(())
    }

    /// Verify the magnetometer die answers with `WHO_AM_I_M == 0x40`.
    ///
    /// The C driver defined `LSM303_WHO_AM_I_M_VALUE` but never checked it, so
    /// this is kept out of [`Lsm303::init`] to leave that sequence unchanged.
    pub fn init_magnetometer_id(&mut self) -> Result<(), Error<E>> {
        let found = self.mag_who_am_i()?;
        if found != register::WHO_AM_I_M_VALUE {
            return Err(Error::WrongDevice {
                expected: register::WHO_AM_I_M_VALUE,
                found,
            });
        }
        Ok(())
    }

    /// Enable tap recognition on all three accelerometer axes.
    ///
    /// Port of `lsm303_enable_taping()`. Writes `CTRL1_A = 0x1F`,
    /// `CTRL3_A = 0x1C`, `TAP_6D_THS_A = 0x01`, `WAKE_UP_THS_A = 0x80`, in that
    /// order.
    ///
    /// The C function took a `double_taping` argument and never read it: it
    /// wrote `WAKE_UP_THS_A = 0x80`, i.e. `SINGLE_DOUBLE_TAP` on, in both cases
    /// and never touched `INT_DUR_A`. This port therefore does not offer the
    /// knob instead of pretending it works; the behaviour is unchanged.
    pub fn enable_tap_detection(&mut self) -> Result<(), Error<E>> {
        self.acc_register_write(register::CTRL1_A, &[tap_defaults::CTRL1.to_byte()])?;
        self.acc_register_write(register::CTRL3_A, &[tap_defaults::CTRL3.to_byte()])?;
        self.acc_register_write(register::TAP_6D_THS_A, &[tap_defaults::TAP_THRESHOLD])?;
        self.acc_register_write(register::WAKE_UP_THS_A, &[tap_defaults::WAKE_UP_THRESHOLD])?;
        Ok(())
    }

    /// Read `TAP_SRC_A`. Port of `lsm303_read_tap()`.
    pub fn read_tap(&mut self) -> Result<u8, Error<E>> {
        let mut data = [0u8; 1];
        self.acc_register_read(register::TAP_SRC_A, &mut data)?;
        Ok(data[0])
    }

    /// Write the three magnetometer configuration registers.
    ///
    /// `CFG_REG_A_M` through `CFG_REG_C_M` are contiguous, so this is one
    /// auto-incrementing write.
    pub fn configure_magnetometer(&mut self, cfg: MagConfig) -> Result<(), Error<E>> {
        self.mag_register_write(
            register::CFG_REG_A_M,
            &[
                cfg.cfg_a.to_byte(),
                cfg.cfg_b.to_byte(),
                cfg.cfg_c.to_byte(),
            ],
        )
    }

    // -- data read-out ---------------------------------------------------

    /// Read `OUT_X_L_A`..`OUT_Z_H_A` as one auto-incrementing burst.
    pub fn read_acceleration(&mut self) -> Result<Acceleration, Error<E>> {
        let mut buf = [0u8; 6];
        self.acc_register_read(register::OUT_X_L_A, &mut buf)?;
        Ok(Acceleration {
            x: i16::from_le_bytes([buf[0], buf[1]]),
            y: i16::from_le_bytes([buf[2], buf[3]]),
            z: i16::from_le_bytes([buf[4], buf[5]]),
        })
    }

    /// Read `OUTX_L_REG_M`..`OUTZ_H_REG_M` as one auto-incrementing burst.
    pub fn read_magnetic_field(&mut self) -> Result<MagneticField, Error<E>> {
        let mut buf = [0u8; 6];
        self.mag_register_read(register::OUTX_L_REG_M, &mut buf)?;
        Ok(MagneticField {
            x: i16::from_le_bytes([buf[0], buf[1]]),
            y: i16::from_le_bytes([buf[2], buf[3]]),
            z: i16::from_le_bytes([buf[4], buf[5]]),
        })
    }

    /// Read `STATUS_A`.
    pub fn acc_status(&mut self) -> Result<u8, Error<E>> {
        let mut data = [0u8; 1];
        self.acc_register_read(register::STATUS_A, &mut data)?;
        Ok(data[0])
    }

    /// Read `STATUS_REG_M`.
    pub fn mag_status(&mut self) -> Result<MagStatus, Error<E>> {
        let mut data = [0u8; 1];
        self.mag_register_read(register::STATUS_REG_M, &mut data)?;
        Ok(MagStatus(data[0]))
    }

    /// Heading in degrees from a fresh accelerometer + magnetometer pair.
    ///
    /// Convenience wrapper over [`heading::tilt_compensated_heading_degrees`];
    /// see there for the angle convention. `None` means the samples carry no
    /// usable direction.
    pub fn read_heading_degrees(&mut self) -> Result<Option<f32>, Error<E>> {
        let acc = self.read_acceleration()?;
        let mag = self.read_magnetic_field()?;
        Ok(heading::tilt_compensated_heading_degrees(&acc, &mag))
    }
}
