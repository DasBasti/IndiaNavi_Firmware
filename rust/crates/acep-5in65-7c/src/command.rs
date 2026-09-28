//! Panel command opcodes and the power/LUT init sequence.
//!
//! Replaces: the command defines in
//! lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.h
//!
//! The C driver never named its opcodes -- it wrote raw bytes inline
//! (`ACEP_5IN65_SendCommand(0x00)` and friends). The names here are the ones
//! the panel controller datasheet uses; the value of each constant, and the
//! payload of every entry in [`INIT_SEQUENCE`], is byte-for-byte what
//! `acep_5in65_7c.c` sent, with the C line number quoted next to it.

/// Panel setting register. C:229
pub const PANEL_SETTING: u8 = 0x00;
/// Power setting register. C:232
pub const POWER_SETTING: u8 = 0x01;
/// Power off. C:303, C:345
pub const POWER_OFF: u8 = 0x02;
/// Power off sequence setting. C:237
pub const POWER_OFF_SEQUENCE_SETTING: u8 = 0x03;
/// Power on. C:297, C:341
pub const POWER_ON: u8 = 0x04;
/// Booster soft start. C:239
pub const BOOSTER_SOFT_START: u8 = 0x06;
/// Deep sleep; only accepted when followed by [`DEEP_SLEEP_CHECK_CODE`]. C:357
pub const DEEP_SLEEP: u8 = 0x07;
/// Start of the frame data stream. C:289, C:325
pub const DATA_START_TRANSMISSION_1: u8 = 0x10;
/// Display refresh. C:300, C:343
pub const DISPLAY_REFRESH: u8 = 0x12;
/// PLL control. C:243
pub const PLL_CONTROL: u8 = 0x30;
/// Temperature sensor calibration. C:245
pub const TEMPERATURE_SENSOR_CALIBRATION: u8 = 0x41;
/// VCOM and data interval setting. Written twice during init. C:247, C:262
pub const VCOM_AND_DATA_INTERVAL_SETTING: u8 = 0x50;
/// TCON setting. C:249
pub const TCON_SETTING: u8 = 0x60;
/// Resolution setting. C:251, C:284, C:320
pub const RESOLUTION_SETTING: u8 = 0x61;
/// Power saving. C:256
pub const POWER_SAVING: u8 = 0xE3;
/// VCM DC setting. C:258
pub const VCM_DC_SETTING: u8 = 0x82;

/// Payload the deep sleep command has to be followed by. C:358
pub const DEEP_SLEEP_CHECK_CODE: u8 = 0xA5;

/// Resolution payload for [`RESOLUTION_SETTING`]: 0x0258 = 600 wide,
/// 0x01C0 = 448 high. C:252-255, repeated verbatim at C:285-288 and C:321-324.
pub const RESOLUTION_DATA: [u8; 4] = [0x02, 0x58, 0x01, 0xC0];

/// One `SendCommand` followed by its `SendData` bytes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Command {
    /// Opcode, sent with D/C low.
    pub cmd: u8,
    /// Payload, sent with D/C high.
    pub data: &'static [u8],
}

/// The init sequence up to the 100 ms pause, in order. C:229-259.
///
/// Sent after the BUSY line has gone high (C:227). Everything after the pause
/// is [`INIT_SEQUENCE_AFTER_DELAY`].
#[rustfmt::skip]
pub const INIT_SEQUENCE: &[Command] = &[
    // C:229-231
    Command { cmd: PANEL_SETTING, data: &[0xEF, 0x08] },
    // C:232-236
    Command { cmd: POWER_SETTING, data: &[0x37, 0x00, 0x23, 0x23] },
    // C:237-238
    Command { cmd: POWER_OFF_SEQUENCE_SETTING, data: &[0x00] },
    // C:239-242
    Command { cmd: BOOSTER_SOFT_START, data: &[0xC7, 0xC7, 0x1D] },
    // C:243-244
    Command { cmd: PLL_CONTROL, data: &[0x3C] },
    // C:245-246
    Command { cmd: TEMPERATURE_SENSOR_CALIBRATION, data: &[0x80] },
    // C:247-248
    Command { cmd: VCOM_AND_DATA_INTERVAL_SETTING, data: &[0x3F] },
    // C:249-250
    Command { cmd: TCON_SETTING, data: &[0x22] },
    // C:251-255
    Command { cmd: RESOLUTION_SETTING, data: &RESOLUTION_DATA },
    // C:256-257
    Command { cmd: POWER_SAVING, data: &[0xAA] },
    // C:258-259
    Command { cmd: VCM_DC_SETTING, data: &[0x80] },
];

/// The tail of the init sequence, sent after [`INIT_DELAY_MS`]. C:262-263.
///
/// Re-writes the VCOM/data-interval register that [`INIT_SEQUENCE`] already
/// set to 0x3F; that double write is in the C driver and is kept.
pub const INIT_SEQUENCE_AFTER_DELAY: &[Command] = &[Command {
    cmd: VCOM_AND_DATA_INTERVAL_SETTING,
    data: &[0x37],
}];

/// Pause in the middle of the init sequence. C:261 is `vTaskDelay(10)`, i.e.
/// 10 FreeRTOS ticks; `CONFIG_FREERTOS_HZ=100` in every `sdkconfig.*` in this
/// repo, so a tick is 10 ms and this is 100 ms.
pub const INIT_DELAY_MS: u32 = 100;
