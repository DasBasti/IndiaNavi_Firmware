//! LSM303 accelerometer + magnetometer driver.
//!
//! Replaces: lib/lsm303/lsm303.c, lib/lsm303/lsm303.h.
//!
//! The C version called the ESP-IDF i2c driver directly and returned
//! `esp_err_t`. Here the driver is generic over `embedded_hal::i2c::I2c` and
//! returns `pm_core::Result`, so it can be unit-tested on the host against a
//! fake bus without any ESP-IDF present.

pub mod accel;
pub mod mag;
pub mod regmap;
pub mod tap;
