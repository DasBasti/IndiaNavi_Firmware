//! Control/configuration register bit layouts.
//!
//! Replaces the `lsm303_acc_ctrl_*` and `lsm303_mag_cfg_*` bitfield unions of
//! `lib/lsm303/lsm303.h`.
//!
//! # Bit ordering
//!
//! The C header declares these as GCC bitfields, e.g.
//!
//! ```c
//! typedef union {
//!     struct { uint8_t odr:3; uint8_t fs:2; uint8_t hf_odr:1; uint8_t bdu:1; };
//!     uint8_t byte;
//! } lsm303_acc_ctrl_1;
//! ```
//!
//! On the little-endian Xtensa/RISC-V targets this firmware builds for, GCC
//! allocates the first-declared field into the *least* significant bits. The
//! layouts below reproduce that exactly, so [`AccCtrl1::to_byte`] and friends
//! produce the same bytes the C driver puts on the wire.
//!
//! Note that this is the reverse of the LSM303AH datasheet field order (the
//! datasheet has `CTRL1_A` as `ODR[3:0] FS[1:0] HF_ODR BDU`, MSB first, and
//! four ODR bits rather than three). The C driver has therefore always been
//! writing shuffled control bytes. This port is deliberately bug-compatible:
//! the task is to keep the same register values and sequencing, not to change
//! the device configuration. See `enable_tap_detection` in `lib.rs`.

/// `CTRL1_A` (0x20) - `lsm303_acc_ctrl_1`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct AccCtrl1 {
    /// Output data rate, bits 2:0 (3 bits, as declared in C).
    pub odr: u8,
    /// Full-scale selection, bits 4:3.
    pub fs: u8,
    /// High-frequency ODR mode enable, bit 5.
    pub hf_odr: bool,
    /// Block data update, bit 6.
    pub bdu: bool,
}

impl AccCtrl1 {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.odr & 0x07)
            | ((self.fs & 0x03) << 3)
            | ((self.hf_odr as u8) << 5)
            | ((self.bdu as u8) << 6)
    }
}

/// `CTRL2_A` (0x21) - `lsm303_acc_ctrl_2`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct AccCtrl2 {
    /// Reboot memory content, bit 0.
    pub boot: bool,
    /// Soft reset, bit 1.
    pub soft_reset: bool,
    /// Access advanced configuration registers 0x2B..0x3F, bit 3.
    pub func_cfg_en: bool,
    /// High-pass filter data selection, bit 4.
    pub fds_slope: bool,
    /// Auto-increment register address on multi-byte access, bit 5.
    ///
    /// Enabled by default in hardware; the multi-byte reads in this driver
    /// rely on it, exactly as the C driver does.
    pub if_add_inc: bool,
    /// Disable the I2C interface, bit 6.
    pub i2c_disable: bool,
    /// Enable SPI read, bit 7.
    pub spi_enable: bool,
}

impl AccCtrl2 {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.boot as u8)
            | ((self.soft_reset as u8) << 1)
            // bit 2 is `_zero` in the C struct
            | ((self.func_cfg_en as u8) << 3)
            | ((self.fds_slope as u8) << 4)
            | ((self.if_add_inc as u8) << 5)
            | ((self.i2c_disable as u8) << 6)
            | ((self.spi_enable as u8) << 7)
    }
}

/// `CTRL3_A` (0x22) - `lsm303_acc_ctrl_3`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct AccCtrl3 {
    /// Self-test mode, bits 1:0.
    pub st: u8,
    /// Tap recognition on X enable, bit 2.
    pub tap_x_en: bool,
    /// Tap recognition on Y enable, bit 3.
    pub tap_y_en: bool,
    /// Tap recognition on Z enable, bit 4.
    pub tap_z_en: bool,
    /// Latch interrupt, bit 5.
    pub lir: bool,
    /// Interrupt active low when set, bit 6.
    pub h_lactive: bool,
    /// Open-drain interrupt pad when set, bit 7.
    pub pp_od: bool,
}

impl AccCtrl3 {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.st & 0x03)
            | ((self.tap_x_en as u8) << 2)
            | ((self.tap_y_en as u8) << 3)
            | ((self.tap_z_en as u8) << 4)
            | ((self.lir as u8) << 5)
            | ((self.h_lactive as u8) << 6)
            | ((self.pp_od as u8) << 7)
    }
}

/// `CTRL4_A` (0x23) - `lsm303_acc_ctrl_4`: INT1 pad routing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct AccCtrl4 {
    /// Single-tap recognition on INT1, bit 1.
    pub int1_s_tap: bool,
    /// Wakeup recognition on INT1, bit 2.
    pub int1_wu: bool,
    /// Free-fall recognition on INT1, bit 3.
    pub int1_ff: bool,
    /// Double-tap recognition on INT1, bit 4.
    pub int1_tap: bool,
    /// 6D recognition on INT1, bit 5.
    pub int1_6d: bool,
    /// FIFO threshold interrupt on INT1, bit 6.
    pub int1_fth: bool,
    /// Data-ready on INT1, bit 7.
    pub int1_drdy: bool,
}

impl AccCtrl4 {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        // bit 0 is `_unused` in the C struct
        ((self.int1_s_tap as u8) << 1)
            | ((self.int1_wu as u8) << 2)
            | ((self.int1_ff as u8) << 3)
            | ((self.int1_tap as u8) << 4)
            | ((self.int1_6d as u8) << 5)
            | ((self.int1_fth as u8) << 6)
            | ((self.int1_drdy as u8) << 7)
    }
}

/// `CTRL5_A` (0x24) - `lsm303_acc_ctrl_5`: INT2 pad routing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct AccCtrl5 {
    /// Pulsed instead of latched data-ready interrupt, bit 0.
    pub drdy_pulsed: bool,
    /// Boot status on INT2, bit 1.
    pub int2_boot: bool,
    /// Mirror everything routed to INT2 on INT1 as well, bit 2.
    pub int2_on_int1: bool,
    /// Tilt event on INT2, bit 3.
    pub int2_tilt: bool,
    /// Significant motion on INT2, bit 4.
    pub int2_sig_mot: bool,
    /// Step detection on INT2, bit 5.
    pub int2_step: bool,
    /// FIFO threshold interrupt on INT2, bit 6.
    pub int2_fth: bool,
    /// Data-ready on INT2, bit 7.
    pub int2_drdy: bool,
}

impl AccCtrl5 {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.drdy_pulsed as u8)
            | ((self.int2_boot as u8) << 1)
            | ((self.int2_on_int1 as u8) << 2)
            | ((self.int2_tilt as u8) << 3)
            | ((self.int2_sig_mot as u8) << 4)
            | ((self.int2_step as u8) << 5)
            | ((self.int2_fth as u8) << 6)
            | ((self.int2_drdy as u8) << 7)
    }
}

/// `INT_CRTL_REG_M` (0x63) - `lsm303_mag_int`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MagInt {
    /// Interrupt recognition on X, bit 0.
    pub xien: bool,
    /// Interrupt recognition on Y, bit 1.
    pub yien: bool,
    /// Interrupt recognition on Z, bit 2.
    pub zien: bool,
    /// Interrupt polarity, bit 5.
    pub iea: bool,
    /// Latch the interrupt instead of pulsing it, bit 6.
    pub iel: bool,
    /// Interrupt enable, bit 7.
    pub ien: bool,
}

impl MagInt {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.xien as u8)
            | ((self.yien as u8) << 1)
            | ((self.zien as u8) << 2)
            // bits 4:3 are `zero` in the C struct
            | ((self.iea as u8) << 5)
            | ((self.iel as u8) << 6)
            | ((self.ien as u8) << 7)
    }
}

/// `INT_SOURCE_REG_M` (0x64) - `lsm303_mag_int_source_b`, read-only.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MagIntSource(pub u8);

impl MagIntSource {
    /// X exceeded the threshold on the positive side, bit 0.
    pub const fn p_th_s_x(self) -> bool {
        self.0 & (1 << 0) != 0
    }
    /// Y exceeded the threshold on the positive side, bit 1.
    pub const fn p_th_s_y(self) -> bool {
        self.0 & (1 << 1) != 0
    }
    /// Z exceeded the threshold on the positive side, bit 2.
    pub const fn p_th_s_z(self) -> bool {
        self.0 & (1 << 2) != 0
    }
    /// X exceeded the threshold on the negative side, bit 3.
    pub const fn n_th_s_x(self) -> bool {
        self.0 & (1 << 3) != 0
    }
    /// Y exceeded the threshold on the negative side, bit 4.
    pub const fn n_th_s_y(self) -> bool {
        self.0 & (1 << 4) != 0
    }
    /// Z exceeded the threshold on the negative side, bit 5.
    pub const fn n_th_s_z(self) -> bool {
        self.0 & (1 << 5) != 0
    }
    /// Magnetic range overflow, bit 6.
    pub const fn mroi(self) -> bool {
        self.0 & (1 << 6) != 0
    }
    /// An interrupt event occurred, bit 7.
    pub const fn int(self) -> bool {
        self.0 & (1 << 7) != 0
    }
}

/// `CFG_REG_A_M` (0x60) - `lsm303_mag_cfg_a`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MagCfgA {
    /// Temperature compensation enable, bit 0.
    pub comp_temp_en: bool,
    /// Reboot memory content, bit 1.
    pub reboot: bool,
    /// Soft reset, bit 2.
    pub soft_rst: bool,
    /// Low-power mode enable, bit 3.
    pub lp: bool,
    /// Output data rate, bits 5:4 (0 = 10 Hz, 1 = 20 Hz, 2 = 50 Hz, 3 = 100 Hz).
    pub odr: u8,
    /// Mode select, bits 7:6 (0 = continuous, 1 = single, 2/3 = idle).
    pub md: u8,
}

impl MagCfgA {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        (self.comp_temp_en as u8)
            | ((self.reboot as u8) << 1)
            | ((self.soft_rst as u8) << 2)
            | ((self.lp as u8) << 3)
            | ((self.odr & 0x03) << 4)
            | ((self.md & 0x03) << 6)
    }
}

/// `CFG_REG_B_M` (0x61) - `lsm303_mag_cfg_b`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MagCfgB {
    /// Offset cancellation in single measurement mode, bit 3.
    pub off_canc_one_shot: bool,
    /// Check data after hard-iron correction for interrupts, bit 4.
    pub int_on_dataoff: bool,
    /// Set-pulse frequency selection, bit 5.
    pub set_freq: bool,
    /// Offset cancellation enable, bit 6.
    pub off_canc: bool,
    /// Low-pass filter enable, bit 7.
    pub lpf: bool,
}

impl MagCfgB {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        // bits 2:0 are `_unused` in the C struct
        ((self.off_canc_one_shot as u8) << 3)
            | ((self.int_on_dataoff as u8) << 4)
            | ((self.set_freq as u8) << 5)
            | ((self.off_canc as u8) << 6)
            | ((self.lpf as u8) << 7)
    }
}

/// `CFG_REG_C_M` (0x62) - `lsm303_mag_cfg_c`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MagCfgC {
    /// Drive the interrupt signal on INT_MAG_PIN, bit 1.
    pub int_mag_pin: bool,
    /// Inhibit the I2C interface, bit 2.
    pub i2c_dis: bool,
    /// Block data update, bit 3.
    pub bdu: bool,
    /// Swap low and high data halves, bit 4.
    pub ble: bool,
    /// Self-test enable, bit 6.
    pub self_test: bool,
    /// Configure DRDY as digital output, bit 7.
    pub int_mag: bool,
}

impl MagCfgC {
    /// Encode to the register byte, matching the C bitfield layout.
    pub const fn to_byte(self) -> u8 {
        // bit 0 is `_unused`, bit 5 is `_zero` in the C struct
        ((self.int_mag_pin as u8) << 1)
            | ((self.i2c_dis as u8) << 2)
            | ((self.bdu as u8) << 3)
            | ((self.ble as u8) << 4)
            | ((self.self_test as u8) << 6)
            | ((self.int_mag as u8) << 7)
    }
}

/// `STATUS_REG_M` (0x67) - `lsm303_mag_status_b`, read-only.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MagStatus(pub u8);

impl MagStatus {
    /// X/Y/Z data overrun, bit 0.
    pub const fn zyxor(self) -> bool {
        self.0 & (1 << 0) != 0
    }
    /// Z data overrun, bit 1.
    pub const fn zor(self) -> bool {
        self.0 & (1 << 1) != 0
    }
    /// Y data overrun, bit 2.
    pub const fn yor(self) -> bool {
        self.0 & (1 << 2) != 0
    }
    /// X data overrun, bit 3.
    pub const fn xor(self) -> bool {
        self.0 & (1 << 3) != 0
    }
    /// New X/Y/Z data available, bit 4.
    pub const fn zyxda(self) -> bool {
        self.0 & (1 << 4) != 0
    }
    /// New Z data available, bit 5.
    pub const fn zda(self) -> bool {
        self.0 & (1 << 5) != 0
    }
    /// New Y data available, bit 6.
    pub const fn yda(self) -> bool {
        self.0 & (1 << 6) != 0
    }
    /// New X data available, bit 7.
    pub const fn xda(self) -> bool {
        self.0 & (1 << 7) != 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// `lsm303_enable_taping()` builds `ctrl_1 = { odr=7, fs=3 }`, which the C
    /// bitfield packs as 0b0001_1111. The firmware even logs it: "CTRL1: 0x1F".
    #[test]
    fn acc_ctrl1_matches_c_bitfield_packing() {
        let ctrl1 = AccCtrl1 {
            odr: 7,
            fs: 3,
            hf_odr: false,
            bdu: false,
        };
        assert_eq!(ctrl1.to_byte(), 0x1F);
    }

    /// `ctrl_3 = { st=0, tap_x_en=1, tap_y_en=1, tap_z_en=1 }` packs as
    /// 0b0001_1100.
    #[test]
    fn acc_ctrl3_matches_c_bitfield_packing() {
        let ctrl3 = AccCtrl3 {
            tap_x_en: true,
            tap_y_en: true,
            tap_z_en: true,
            ..AccCtrl3::default()
        };
        assert_eq!(ctrl3.to_byte(), 0x1C);
    }

    #[test]
    fn acc_ctrl1_fields_land_in_the_declared_bits() {
        assert_eq!(
            AccCtrl1 {
                odr: 0x07,
                ..Default::default()
            }
            .to_byte(),
            0b0000_0111
        );
        assert_eq!(
            AccCtrl1 {
                fs: 0x03,
                ..Default::default()
            }
            .to_byte(),
            0b0001_1000
        );
        assert_eq!(
            AccCtrl1 {
                hf_odr: true,
                ..Default::default()
            }
            .to_byte(),
            0b0010_0000
        );
        assert_eq!(
            AccCtrl1 {
                bdu: true,
                ..Default::default()
            }
            .to_byte(),
            0b0100_0000
        );
    }

    #[test]
    fn acc_ctrl2_skips_the_reserved_bit_2() {
        assert_eq!(
            AccCtrl2 {
                boot: true,
                soft_reset: true,
                func_cfg_en: true,
                if_add_inc: true,
                ..Default::default()
            }
            .to_byte(),
            0b0010_1011
        );
    }

    #[test]
    fn mag_cfg_a_fields_land_in_the_declared_bits() {
        assert_eq!(
            MagCfgA {
                comp_temp_en: true,
                ..Default::default()
            }
            .to_byte(),
            0b0000_0001
        );
        assert_eq!(
            MagCfgA {
                odr: 3,
                ..Default::default()
            }
            .to_byte(),
            0b0011_0000
        );
        assert_eq!(
            MagCfgA {
                md: 2,
                ..Default::default()
            }
            .to_byte(),
            0b1000_0000
        );
    }

    #[test]
    fn mag_cfg_b_and_c_skip_their_reserved_bits() {
        assert_eq!(
            MagCfgB {
                off_canc: true,
                lpf: true,
                ..Default::default()
            }
            .to_byte(),
            0b1100_0000
        );
        assert_eq!(
            MagCfgC {
                bdu: true,
                ..Default::default()
            }
            .to_byte(),
            0b0000_1000
        );
        assert_eq!(
            MagCfgC {
                self_test: true,
                int_mag: true,
                ..Default::default()
            }
            .to_byte(),
            0b1100_0000
        );
    }

    #[test]
    fn acc_ctrl4_and_ctrl5_fields_land_in_the_declared_bits() {
        // CTRL4_A bit 0 is reserved in the C struct.
        assert_eq!(
            AccCtrl4 {
                int1_s_tap: true,
                ..Default::default()
            }
            .to_byte(),
            0b0000_0010
        );
        assert_eq!(
            AccCtrl4 {
                int1_drdy: true,
                ..Default::default()
            }
            .to_byte(),
            0b1000_0000
        );
        // CTRL5_A starts at bit 0.
        assert_eq!(
            AccCtrl5 {
                drdy_pulsed: true,
                ..Default::default()
            }
            .to_byte(),
            0b0000_0001
        );
        assert_eq!(
            AccCtrl5 {
                int2_drdy: true,
                ..Default::default()
            }
            .to_byte(),
            0b1000_0000
        );
    }

    #[test]
    fn mag_int_skips_the_two_reserved_bits() {
        assert_eq!(
            MagInt {
                xien: true,
                yien: true,
                zien: true,
                ..Default::default()
            }
            .to_byte(),
            0b0000_0111
        );
        assert_eq!(
            MagInt {
                iea: true,
                iel: true,
                ien: true,
                ..Default::default()
            }
            .to_byte(),
            0b1110_0000
        );
    }

    #[test]
    fn mag_int_source_decodes_every_flag() {
        assert!(MagIntSource(0b0000_0001).p_th_s_x());
        assert!(MagIntSource(0b0000_0010).p_th_s_y());
        assert!(MagIntSource(0b0000_0100).p_th_s_z());
        assert!(MagIntSource(0b0000_1000).n_th_s_x());
        assert!(MagIntSource(0b0001_0000).n_th_s_y());
        assert!(MagIntSource(0b0010_0000).n_th_s_z());
        assert!(MagIntSource(0b0100_0000).mroi());
        assert!(MagIntSource(0b1000_0000).int());
        assert!(!MagIntSource(0b0000_0000).int());
    }

    #[test]
    fn mag_status_decodes_every_flag() {
        assert!(MagStatus(0b0000_0010).zor());
        assert!(MagStatus(0b0000_0100).yor());
        assert!(MagStatus(0b0000_1000).xor());
        assert!(MagStatus(0b0010_0000).zda());
        assert!(MagStatus(0b0100_0000).yda());
        assert!(MagStatus(0b1000_0000).xda());
    }

    #[test]
    fn mag_status_decodes_data_ready_and_overrun() {
        assert!(MagStatus(0b0001_0000).zyxda());
        assert!(!MagStatus(0b0001_0000).zyxor());
        assert!(MagStatus(0b0000_0001).zyxor());
        assert!(!MagStatus(0b0000_0001).zyxda());
    }
}
