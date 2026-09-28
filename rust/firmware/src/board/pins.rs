//! Pin assignments for both IndiaNavi boards.
//!
//! Replaces: include/pins.h
//!
//! `pins.h` is a flat list of `#define`s inside an `#ifdef ESP_S3` / `#else`
//! pair. Here both branches exist as two `const PinMap` values that are
//! *always* compiled, and the cargo feature only decides which one [`PINS`]
//! aliases. That is what lets the unit tests at the bottom of this file check
//! every pin of *both* boards in a single host `cargo test`, instead of only
//! the branch the current feature selects.
//!
//! Numbers are `i32`, not `u8`, for the same reason the C header uses plain
//! ints: `EINK_SPI_MISO` is `-1`, ESP-IDF's `GPIO_NUM_NC`, meaning "not
//! connected".

/// "Not connected", ESP-IDF's `GPIO_NUM_NC`. `pins.h` writes this as `-1`.
pub const NC: i32 = -1;

/// Every pin `include/pins.h` defines, for one board.
///
/// Field names are the `pins.h` macro names lowercased, so a reviewer can
/// diff this file against the header line by line.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PinMap {
    /// Console UART, `UART_TX`.
    pub uart_tx: i32,
    /// Console UART, `UART_RX`.
    pub uart_rx: i32,

    /// GPS rail enable, active low (`GPS_VCC_nEN`).
    pub gps_vcc_nen: i32,
    /// GPS UART2 TX, `GPS_UART2_TX`.
    pub gps_uart2_tx: i32,
    /// GPS UART2 RX, `GPS_UART2_RX`.
    pub gps_uart2_rx: i32,

    /// ePaper SPI clock, `EINK_SPI_CLK`.
    pub eink_spi_clk: i32,
    /// ePaper SPI MOSI, `EINK_SPI_MOSI`.
    pub eink_spi_mosi: i32,
    /// ePaper SPI MISO, `EINK_SPI_MISO`. Always [`NC`]: the panel is write-only.
    pub eink_spi_miso: i32,
    /// ePaper chip select, active low (`EINK_SPI_nCS`).
    pub eink_spi_ncs: i32,
    /// ePaper data/command select, `EINK_DC`.
    pub eink_dc: i32,
    /// ePaper rail enable, active low (`EINK_VCC_nEN`).
    pub eink_vcc_nen: i32,
    /// ePaper busy line, `EINK_BUSY`.
    pub eink_busy: i32,

    /// Battery sense, `VBAT_ADC`.
    ///
    /// Careful: this is an **ADC1 channel number**, not a GPIO number --
    /// `src/esp32/main.c` passes it straight to `adc_oneshot_config_channel`
    /// and `adc_oneshot_read`. See [`crate::board::adc`] for the channel to
    /// GPIO mapping.
    pub vbat_adc: i32,
    /// Charger/USB sense, `VIN_ADC`. Also an ADC1 channel number, see
    /// [`PinMap::vbat_adc`].
    pub vin_adc: i32,

    /// SD rail enable, active low (`SD_VCC_nEN`).
    pub sd_vcc_nen: i32,
    /// SD data line 0, `SD_SPI_D0`.
    pub sd_spi_d0: i32,
    /// SD data line 1, `SD_SPI_D1`.
    pub sd_spi_d1: i32,
    /// SD data line 2, `SD_SPI_D2`.
    pub sd_spi_d2: i32,
    /// SD data line 3, `SD_SPI_D3`.
    pub sd_spi_d3: i32,
    /// SD clock, `SD_SPI_CLK`.
    pub sd_spi_clk: i32,
    /// SD command line, `SD_SPI_nCS`. In SDMMC mode this is CMD, see
    /// [`crate::board::spi`].
    pub sd_spi_ncs: i32,
    /// SD card detect, active low (`SD_CARD_nDET`).
    pub sd_card_ndet: i32,

    /// User button, `BTN`.
    pub btn: i32,
    /// Level the button reads when pressed, `BTN_LEVEL`.
    pub btn_level: i32,
    /// Status LED, `LED`.
    pub led: i32,

    /// I2C peripheral index, `I2C_MASTER_NUM` (not a pin).
    pub i2c_master_num: i32,
    /// I2C data, `I2C_SDA`.
    pub i2c_sda: i32,
    /// I2C clock, `I2C_SCL`.
    pub i2c_scl: i32,
    /// Accelerometer interrupt, `I2C_INT`.
    pub i2c_int: i32,
    /// Level `I2C_INT` asserts, `I2C_INT_LEVEL`.
    pub i2c_int_level: i32,
}

/// ESP32-S3 board `indianavi-s3-n16r8`: the `#ifdef ESP_S3` branch of
/// `include/pins.h`.
pub const ESP32S3: PinMap = PinMap {
    uart_tx: 43,
    uart_rx: 44,

    gps_vcc_nen: 15,
    gps_uart2_tx: 1,
    gps_uart2_rx: 40,

    eink_spi_clk: 39,
    eink_spi_mosi: 13,
    eink_spi_miso: NC,
    eink_spi_ncs: 38,
    eink_dc: 17,
    eink_vcc_nen: 18,
    eink_busy: 5,

    vbat_adc: 5,
    vin_adc: 6,

    sd_vcc_nen: 14,
    sd_spi_d0: 48,
    sd_spi_d1: 21,
    sd_spi_d2: 20,
    sd_spi_d3: 9,
    sd_spi_clk: 19,
    sd_spi_ncs: 47,
    sd_card_ndet: 4,

    btn: 8,
    btn_level: 0,
    led: 16,

    i2c_master_num: 0,
    i2c_sda: 0,
    i2c_scl: 2,
    i2c_int: 42,
    i2c_int_level: 0,
};

/// Original REV2 ESP32 board: the `#else` branch of `include/pins.h`.
pub const ESP32_REV2: PinMap = PinMap {
    uart_tx: 1,
    uart_rx: 3,

    gps_vcc_nen: 32,
    gps_uart2_tx: 23,
    gps_uart2_rx: 19,

    eink_spi_clk: 18,
    eink_spi_mosi: 17,
    eink_spi_miso: NC,
    eink_spi_ncs: 5,
    eink_dc: 25,
    eink_vcc_nen: 26,
    eink_busy: 39,

    vbat_adc: 6,
    vin_adc: 7,

    sd_vcc_nen: 16,
    sd_spi_d0: 2,
    sd_spi_d1: 4,
    sd_spi_d2: 12,
    sd_spi_d3: 13,
    sd_spi_clk: 14,
    sd_spi_ncs: 15,
    sd_card_ndet: 36,

    btn: 27,
    btn_level: 0,
    led: 33,

    i2c_master_num: 0,
    i2c_sda: 0,
    i2c_scl: 22,
    i2c_int: 21,
    i2c_int_level: 0,
};

/// The pin map for the board this image is being built for.
///
/// Replaces the `#ifdef ESP_S3` switch: `board-esp32s3` is the default feature
/// and wins if both are somehow enabled, matching `src/main.rs`'s `BOARD`.
#[cfg(feature = "board-esp32s3")]
pub const PINS: PinMap = ESP32S3;
#[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
pub const PINS: PinMap = ESP32_REV2;

#[cfg(test)]
mod tests {
    use super::*;

    /// Transcribed independently from `include/pins.h`, ESP_S3 branch, so this
    /// test pins the numbers rather than agreeing with the table it checks.
    #[test]
    fn esp32s3_map_matches_pins_h() {
        let p = ESP32S3;
        assert_eq!(p.uart_tx, 43);
        assert_eq!(p.uart_rx, 44);

        assert_eq!(p.gps_vcc_nen, 15);
        assert_eq!(p.gps_uart2_tx, 1);
        assert_eq!(p.gps_uart2_rx, 40);

        assert_eq!(p.eink_spi_clk, 39);
        assert_eq!(p.eink_spi_mosi, 13);
        assert_eq!(p.eink_spi_miso, -1);
        assert_eq!(p.eink_spi_ncs, 38);
        assert_eq!(p.eink_dc, 17);
        assert_eq!(p.eink_vcc_nen, 18);
        assert_eq!(p.eink_busy, 5);

        assert_eq!(p.vbat_adc, 5);
        assert_eq!(p.vin_adc, 6);

        assert_eq!(p.sd_vcc_nen, 14);
        assert_eq!(p.sd_spi_d0, 48);
        assert_eq!(p.sd_spi_d1, 21);
        assert_eq!(p.sd_spi_d2, 20);
        assert_eq!(p.sd_spi_d3, 9);
        assert_eq!(p.sd_spi_clk, 19);
        assert_eq!(p.sd_spi_ncs, 47);
        assert_eq!(p.sd_card_ndet, 4);

        assert_eq!(p.btn, 8);
        assert_eq!(p.btn_level, 0);
        assert_eq!(p.led, 16);

        assert_eq!(p.i2c_master_num, 0);
        assert_eq!(p.i2c_sda, 0);
        assert_eq!(p.i2c_scl, 2);
        assert_eq!(p.i2c_int, 42);
        assert_eq!(p.i2c_int_level, 0);
    }

    /// Transcribed independently from `include/pins.h`, `#else` branch.
    #[test]
    fn esp32_rev2_map_matches_pins_h() {
        let p = ESP32_REV2;
        assert_eq!(p.uart_tx, 1);
        assert_eq!(p.uart_rx, 3);

        assert_eq!(p.gps_vcc_nen, 32);
        assert_eq!(p.gps_uart2_tx, 23);
        assert_eq!(p.gps_uart2_rx, 19);

        assert_eq!(p.eink_spi_clk, 18);
        assert_eq!(p.eink_spi_mosi, 17);
        assert_eq!(p.eink_spi_miso, -1);
        assert_eq!(p.eink_spi_ncs, 5);
        assert_eq!(p.eink_dc, 25);
        assert_eq!(p.eink_vcc_nen, 26);
        assert_eq!(p.eink_busy, 39);

        assert_eq!(p.vbat_adc, 6);
        assert_eq!(p.vin_adc, 7);

        assert_eq!(p.sd_vcc_nen, 16);
        assert_eq!(p.sd_spi_d0, 2);
        assert_eq!(p.sd_spi_d1, 4);
        assert_eq!(p.sd_spi_d2, 12);
        assert_eq!(p.sd_spi_d3, 13);
        assert_eq!(p.sd_spi_clk, 14);
        assert_eq!(p.sd_spi_ncs, 15);
        assert_eq!(p.sd_card_ndet, 36);

        assert_eq!(p.btn, 27);
        assert_eq!(p.btn_level, 0);
        assert_eq!(p.led, 33);

        assert_eq!(p.i2c_master_num, 0);
        assert_eq!(p.i2c_sda, 0);
        assert_eq!(p.i2c_scl, 22);
        assert_eq!(p.i2c_int, 21);
        assert_eq!(p.i2c_int_level, 0);
    }

    /// The two boards must not accidentally be the same table -- that would
    /// make the feature switch silently useless.
    #[test]
    fn the_two_boards_differ() {
        assert_ne!(ESP32S3, ESP32_REV2);
    }

    /// The feature selects the right table. Only the active feature can be
    /// checked, which is exactly why the two tests above exist.
    #[test]
    fn feature_selects_the_right_map() {
        #[cfg(feature = "board-esp32s3")]
        assert_eq!(PINS, ESP32S3);
        #[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
        assert_eq!(PINS, ESP32_REV2);
    }

    /// `pins.h` gives the panel no MISO on either board.
    #[test]
    fn epaper_is_write_only_on_both_boards() {
        assert_eq!(ESP32S3.eink_spi_miso, NC);
        assert_eq!(ESP32_REV2.eink_spi_miso, NC);
    }
}
