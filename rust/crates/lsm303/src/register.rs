//! LSM303AH register map.
//!
//! Replaces the `enum lsm303_register` and the `LSM303_*` defines of
//! `lib/lsm303/lsm303.c` / `lib/lsm303/lsm303.h`.
//!
//! Addresses and ordering are a one-to-one transcription of the C enum, which
//! uses implicit `+ 1` continuation after the explicitly numbered entries.

// The constants below are a transcription of the datasheet/C register names;
// a doc comment on each would only repeat the name.
#![allow(missing_docs)]

/// I2C address of the accelerometer die (`LSM303_ACC_ADDR`).
pub const ACC_ADDR: u8 = 0x1D;
/// I2C address of the magnetometer die (`LSM303_MAG_ADDR`).
pub const MAG_ADDR: u8 = 0x1E;

/// Expected content of [`WHO_AM_I_A`] (`LSM303_WHO_AM_I_A_VALUE`).
pub const WHO_AM_I_A_VALUE: u8 = 0x43;
/// Expected content of [`WHO_AM_I_M`] (`LSM303_WHO_AM_I_M_VALUE`).
pub const WHO_AM_I_M_VALUE: u8 = 0x40;

// -- Accelerometer -------------------------------------------------------

pub const MODULE_8BIT_A: u8 = 0x0C;
pub const WHO_AM_I_A: u8 = 0x0F;
pub const CTRL1_A: u8 = 0x20;
pub const CTRL2_A: u8 = 0x21;
pub const CTRL3_A: u8 = 0x22;
pub const CTRL4_A: u8 = 0x23;
pub const CTRL5_A: u8 = 0x24;
pub const FIFO_CTRL_A: u8 = 0x25;
pub const OUT_T_A: u8 = 0x26;
pub const STATUS_A: u8 = 0x27;
pub const OUT_X_L_A: u8 = 0x28;
pub const OUT_X_H_A: u8 = 0x29;
pub const OUT_Y_L_A: u8 = 0x2A;
pub const OUT_Y_H_A: u8 = 0x2B;
pub const OUT_Z_L_A: u8 = 0x2C;
pub const OUT_Z_H_A: u8 = 0x2D;
pub const FIFO_THS_A: u8 = 0x2E;
pub const FIFO_SRC_A: u8 = 0x2F;
pub const FIFO_SAMPLES_A: u8 = 0x30;
pub const TAP_6D_THS_A: u8 = 0x31;
pub const INT_DUR_A: u8 = 0x32;
pub const WAKE_UP_THS_A: u8 = 0x33;
pub const WAKE_UP_DUR_A: u8 = 0x34;
pub const FREE_FALL_A: u8 = 0x35;
pub const STATUS_DUP_A: u8 = 0x36;
pub const WAKE_UP_SRC_A: u8 = 0x37;
pub const TAP_SRC_A: u8 = 0x38;
pub const SIXD_SRC_A: u8 = 0x39;
pub const STEP_COUNTER_MINTHS_A: u8 = 0x3A;
pub const STEP_COUNTER_L_A: u8 = 0x3B;
pub const STEP_COUNTER_H_A: u8 = 0x3C;
pub const FUNC_CK_GATE_A: u8 = 0x3D;
pub const FUNC_SRC_A: u8 = 0x3E;
pub const FUNC_CTRL_A: u8 = 0x3F;

// -- Magnetometer --------------------------------------------------------

pub const OFFSET_X_REG_L_M: u8 = 0x45;
pub const OFFSET_X_REG_H_M: u8 = 0x46;
pub const OFFSET_Y_REG_L_M: u8 = 0x47;
pub const OFFSET_Y_REG_H_M: u8 = 0x48;
pub const OFFSET_Z_REG_L_M: u8 = 0x49;
pub const OFFSET_Z_REG_H_M: u8 = 0x4A;
pub const WHO_AM_I_M: u8 = 0x4F;
pub const CFG_REG_A_M: u8 = 0x60;
pub const CFG_REG_B_M: u8 = 0x61;
pub const CFG_REG_C_M: u8 = 0x62;
pub const INT_CRTL_REG_M: u8 = 0x63;
pub const INT_SOURCE_REG_M: u8 = 0x64;
pub const INT_THS_L_REG_M: u8 = 0x65;
pub const INT_THS_H_REG_M: u8 = 0x66;
pub const STATUS_REG_M: u8 = 0x67;
pub const OUTX_L_REG_M: u8 = 0x68;
pub const OUTX_H_REG_M: u8 = 0x69;
pub const OUTY_L_REG_M: u8 = 0x6A;
pub const OUTY_H_REG_M: u8 = 0x6B;
pub const OUTZ_L_REG_M: u8 = 0x6C;
pub const OUTZ_H_REG_M: u8 = 0x6D;

#[cfg(test)]
mod tests {
    use super::*;

    /// The C enum relies on implicit continuation; assert the resulting
    /// addresses so a future edit cannot silently shift the block.
    #[test]
    fn accelerometer_block_is_contiguous_from_ctrl1() {
        let block = [
            CTRL1_A,
            CTRL2_A,
            CTRL3_A,
            CTRL4_A,
            CTRL5_A,
            FIFO_CTRL_A,
            OUT_T_A,
            STATUS_A,
            OUT_X_L_A,
            OUT_X_H_A,
            OUT_Y_L_A,
            OUT_Y_H_A,
            OUT_Z_L_A,
            OUT_Z_H_A,
            FIFO_THS_A,
            FIFO_SRC_A,
            FIFO_SAMPLES_A,
            TAP_6D_THS_A,
            INT_DUR_A,
            WAKE_UP_THS_A,
            WAKE_UP_DUR_A,
            FREE_FALL_A,
            STATUS_DUP_A,
            WAKE_UP_SRC_A,
            TAP_SRC_A,
            SIXD_SRC_A,
            STEP_COUNTER_MINTHS_A,
            STEP_COUNTER_L_A,
            STEP_COUNTER_H_A,
            FUNC_CK_GATE_A,
            FUNC_SRC_A,
            FUNC_CTRL_A,
        ];
        for (i, reg) in block.iter().enumerate() {
            assert_eq!(*reg, CTRL1_A + i as u8, "register {i} after CTRL1_A");
        }
        assert_eq!(FUNC_CTRL_A, 0x3F);
    }

    #[test]
    fn magnetometer_block_is_contiguous_from_cfg_a() {
        let block = [
            CFG_REG_A_M,
            CFG_REG_B_M,
            CFG_REG_C_M,
            INT_CRTL_REG_M,
            INT_SOURCE_REG_M,
            INT_THS_L_REG_M,
            INT_THS_H_REG_M,
            STATUS_REG_M,
            OUTX_L_REG_M,
            OUTX_H_REG_M,
            OUTY_L_REG_M,
            OUTY_H_REG_M,
            OUTZ_L_REG_M,
            OUTZ_H_REG_M,
        ];
        for (i, reg) in block.iter().enumerate() {
            assert_eq!(
                *reg,
                CFG_REG_A_M + i as u8,
                "register {i} after CFG_REG_A_M"
            );
        }
        assert_eq!(OUTZ_H_REG_M, 0x6D);
    }

    #[test]
    fn offset_block_is_contiguous() {
        let block = [
            OFFSET_X_REG_L_M,
            OFFSET_X_REG_H_M,
            OFFSET_Y_REG_L_M,
            OFFSET_Y_REG_H_M,
            OFFSET_Z_REG_L_M,
            OFFSET_Z_REG_H_M,
        ];
        for (i, reg) in block.iter().enumerate() {
            assert_eq!(*reg, OFFSET_X_REG_L_M + i as u8);
        }
    }
}
