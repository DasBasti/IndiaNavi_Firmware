//! Bus-level tests for the LSM303AH driver.
//!
//! These assert the exact I2C traffic, so they fail if the port ever drifts
//! from the register values and sequencing of `lib/lsm303/lsm303.c`.

use embedded_hal_mock::eh1::i2c::{Mock as I2cMock, Transaction as I2cTransaction};
use lsm303::{register, Acceleration, Error, Lsm303, MagConfig, MagneticField};

const ACC: u8 = 0x1D;
const MAG: u8 = 0x1E;

/// Every register read in the C driver was an address-write transaction
/// followed by a *separate* read transaction, not a repeated start.
fn read_pair(addr: u8, reg: u8, data: Vec<u8>) -> [I2cTransaction; 2] {
    [
        I2cTransaction::write(addr, vec![reg]),
        I2cTransaction::read(addr, data),
    ]
}

#[test]
fn addresses_match_the_c_driver() {
    assert_eq!(register::ACC_ADDR, ACC);
    assert_eq!(register::MAG_ADDR, MAG);
    assert_eq!(register::WHO_AM_I_A_VALUE, 0x43);
    assert_eq!(register::WHO_AM_I_M_VALUE, 0x40);
}

#[test]
fn init_reads_who_am_i_a_and_accepts_0x43() {
    let expectations = read_pair(ACC, register::WHO_AM_I_A, vec![0x43]);
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor.init().unwrap();

    sensor.release().done();
}

#[test]
fn init_rejects_a_foreign_who_am_i() {
    let expectations = read_pair(ACC, register::WHO_AM_I_A, vec![0x33]);
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(
        sensor.init(),
        Err(Error::WrongDevice {
            expected: 0x43,
            found: 0x33,
        })
    );

    sensor.release().done();
}

#[test]
fn magnetometer_id_check_uses_who_am_i_m() {
    let expectations = read_pair(MAG, register::WHO_AM_I_M, vec![0x40]);
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor.init_magnetometer_id().unwrap();

    sensor.release().done();
}

/// `lsm303_enable_taping()` wrote exactly these four registers, in this order,
/// with these values. The C source even logs "CTRL1: 0x1F" / "CTRL3: 0x1C".
#[test]
fn enable_tap_detection_writes_the_c_init_sequence() {
    let expectations = [
        I2cTransaction::write(ACC, vec![0x20, 0x1F]), // CTRL1_A
        I2cTransaction::write(ACC, vec![0x22, 0x1C]), // CTRL3_A
        I2cTransaction::write(ACC, vec![0x31, 0x01]), // TAP_6D_THS_A
        I2cTransaction::write(ACC, vec![0x33, 0x80]), // WAKE_UP_THS_A
    ];
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor.enable_tap_detection().unwrap();

    sensor.release().done();
}

/// The full power-on path a firmware caller takes: identify, then arm taps.
#[test]
fn init_then_enable_tap_detection_is_one_uninterrupted_sequence() {
    let mut expectations = vec![
        I2cTransaction::write(ACC, vec![0x0F]),
        I2cTransaction::read(ACC, vec![0x43]),
    ];
    expectations.extend([
        I2cTransaction::write(ACC, vec![0x20, 0x1F]),
        I2cTransaction::write(ACC, vec![0x22, 0x1C]),
        I2cTransaction::write(ACC, vec![0x31, 0x01]),
        I2cTransaction::write(ACC, vec![0x33, 0x80]),
    ]);
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor.init().unwrap();
    sensor.enable_tap_detection().unwrap();

    sensor.release().done();
}

#[test]
fn read_tap_reads_tap_src_a() {
    let expectations = read_pair(ACC, 0x38, vec![0x48]);
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(sensor.read_tap().unwrap(), 0x48);

    sensor.release().done();
}

#[test]
fn configure_magnetometer_writes_cfg_a_b_c_in_one_burst() {
    // Defaults: temperature compensation on (bit 0), 10 Hz, continuous mode;
    // offset cancellation (bit 6); block data update (bit 3).
    let expectations = [I2cTransaction::write(MAG, vec![0x60, 0x01, 0x40, 0x08])];
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor.configure_magnetometer(MagConfig::default()).unwrap();

    sensor.release().done();
}

/// -1234 == 0xFB2E, 567 == 0x0237, 16000 == 0x3E80, little endian per axis.
#[test]
fn read_acceleration_decodes_a_known_sample() {
    let expectations = read_pair(
        ACC,
        register::OUT_X_L_A,
        vec![0x2E, 0xFB, 0x37, 0x02, 0x80, 0x3E],
    );
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(
        sensor.read_acceleration().unwrap(),
        Acceleration {
            x: -1234,
            y: 567,
            z: 16000,
        }
    );

    sensor.release().done();
}

#[test]
fn read_magnetic_field_decodes_a_known_sample() {
    // 3000 == 0x0BB8, -2000 == 0xF830, 500 == 0x01F4.
    let expectations = read_pair(
        MAG,
        register::OUTX_L_REG_M,
        vec![0xB8, 0x0B, 0x30, 0xF8, 0xF4, 0x01],
    );
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(
        sensor.read_magnetic_field().unwrap(),
        MagneticField {
            x: 3000,
            y: -2000,
            z: 500,
        }
    );

    sensor.release().done();
}

#[test]
fn status_registers_are_read_from_the_right_die() {
    let mut expectations = read_pair(ACC, register::STATUS_A, vec![0x0F]).to_vec();
    expectations.extend(read_pair(MAG, register::STATUS_REG_M, vec![0x10]));
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(sensor.acc_status().unwrap(), 0x0F);
    let status = sensor.mag_status().unwrap();
    assert!(status.zyxda());
    assert!(!status.zyxor());

    sensor.release().done();
}

/// A level board with the field 45 degrees off +X must read back 45 degrees,
/// end to end from raw bytes.
#[test]
fn read_heading_degrees_decodes_and_computes_from_raw_bytes() {
    let mut expectations = read_pair(
        ACC,
        register::OUT_X_L_A,
        // x = 0, y = 0, z = 1000 (0x03E8): level.
        vec![0x00, 0x00, 0x00, 0x00, 0xE8, 0x03],
    )
    .to_vec();
    expectations.extend(read_pair(
        MAG,
        register::OUTX_L_REG_M,
        // x = 1000, y = 1000, z = 0.
        vec![0xE8, 0x03, 0xE8, 0x03, 0x00, 0x00],
    ));
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    let heading = sensor.read_heading_degrees().unwrap().unwrap();
    assert!(
        (heading - 45.0).abs() < 0.1,
        "expected 45 degrees, got {heading}"
    );

    sensor.release().done();
}

#[test]
fn raw_register_helpers_target_the_requested_die() {
    let mut expectations = vec![I2cTransaction::write(ACC, vec![0x25, 0x00])];
    expectations.push(I2cTransaction::write(MAG, vec![0x65, 0x10]));
    expectations.extend(read_pair(MAG, 0x45, vec![0x01, 0x02]));
    let i2c = I2cMock::new(&expectations);
    let mut sensor = Lsm303::new(i2c);

    sensor
        .acc_register_write_byte(register::FIFO_CTRL_A, 0x00)
        .unwrap();
    sensor
        .mag_register_write_byte(register::INT_THS_L_REG_M, 0x10)
        .unwrap();
    let mut offsets = [0u8; 2];
    sensor
        .mag_register_read(register::OFFSET_X_REG_L_M, &mut offsets)
        .unwrap();
    assert_eq!(offsets, [0x01, 0x02]);

    sensor.release().done();
}

#[test]
fn oversized_register_write_is_rejected_without_touching_the_bus() {
    let i2c = I2cMock::new(&[]);
    let mut sensor = Lsm303::new(i2c);

    assert_eq!(
        sensor.acc_register_write(register::CTRL1_A, &[0u8; 7]),
        Err(Error::WriteTooLong)
    );

    sensor.release().done();
}

#[test]
fn bus_errors_propagate_instead_of_aborting() {
    use embedded_hal::i2c::{Error as _, ErrorKind, ErrorType, I2c, Operation};

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    struct Nack;
    impl embedded_hal::i2c::Error for Nack {
        fn kind(&self) -> ErrorKind {
            ErrorKind::NoAcknowledge(embedded_hal::i2c::NoAcknowledgeSource::Address)
        }
    }
    struct DeadBus;
    impl ErrorType for DeadBus {
        type Error = Nack;
    }
    impl I2c for DeadBus {
        fn transaction(
            &mut self,
            _address: u8,
            _operations: &mut [Operation<'_>],
        ) -> Result<(), Self::Error> {
            Err(Nack)
        }
    }

    let mut sensor = Lsm303::new(DeadBus);
    assert_eq!(sensor.init(), Err(Error::Bus(Nack)));
    assert_eq!(
        Nack.kind(),
        ErrorKind::NoAcknowledge(embedded_hal::i2c::NoAcknowledgeSource::Address)
    );
}
