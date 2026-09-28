//! The two serial buses on the board: the ePaper SPI bus and the SD card bus.
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/hw/spi.h,
//! lib/Platinenmacher_HAL_ESP32/hw/esp32/spi.c, the `spi_bus_config_t` /
//! `spi_device_interface_config_t` in
//! lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.c:175-197, the
//! `acep_5in65_dev_t` literal in src/esp32/gui.c:62-69, and the
//! `sdmmc_slot_config_t` in src/esp32/sd.c:35-53
//!
//! `hw/spi.h` and `hw/esp32/spi.c` are almost entirely dead: `spi_init` returns
//! a zeroed struct, `spi_transmit` is declared and never defined, and nothing
//! in the C tree calls either. The ePaper driver talks to `spi_bus_add_device`
//! directly. So what actually gets ported is the two config literals, as the
//! pure descriptors below, plus the `esp-idf-hal` construction that turns them
//! into `embedded-hal` handles.
//!
//! # The SD "SPI" bus is not SPI
//!
//! `include/pins.h` names the SD lines `SD_SPI_*`, but `src/esp32/sd.c` mounts
//! the card through `esp_vfs_fat_sdmmc_mount` on the **SDMMC** peripheral in
//! 4-bit mode -- `SD_SPI_nCS` is SDMMC CMD and `SD_SPI_D1..D3` are the extra
//! data lines, which plain SPI has no use for. [`SD_BUS`] therefore describes
//! an SDMMC slot, not an SPI bus, and keeps the misleading C pin names only in
//! the doc comments. Nothing here forces an SPI-mode fallback; if one is ever
//! wanted, it is a new descriptor, not a reinterpretation of this one.

use super::pins::{PinMap, PINS};

/// SPI mode, as `spi_device_interface_config_t::mode`.
///
/// The panel is mode 0 (`acep_5in65_7c.c:184`): idle-low clock, sample on the
/// rising edge.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum SpiMode {
    /// CPOL 0, CPHA 0.
    Mode0 = 0,
    /// CPOL 0, CPHA 1.
    Mode1 = 1,
    /// CPOL 1, CPHA 0.
    Mode2 = 2,
    /// CPOL 1, CPHA 1.
    Mode3 = 3,
}

/// Which SPI peripheral a bus lives on.
///
/// The panel uses `SPI3_HOST` on both boards (src/esp32/gui.c:69). `SPI1_HOST`
/// is the flash controller and is deliberately absent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum SpiHost {
    /// `SPI2_HOST`, a.k.a. HSPI on the original ESP32.
    Spi2 = 2,
    /// `SPI3_HOST`, a.k.a. VSPI on the original ESP32.
    Spi3 = 3,
}

/// Everything needed to bring up one SPI bus and its single device.
///
/// The C code split this across a `spi_bus_config_t`, a
/// `spi_device_interface_config_t` and the `acep_5in65_dev_t` literal in
/// `gui.c`; all three described the same one bus with the same one device on it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpiBus {
    /// Peripheral, `acep_5in65_dev_t::host`.
    pub host: SpiHost,
    /// Clock, `spi_bus_config_t::sclk_io_num`.
    pub sclk: i32,
    /// Controller out, `spi_bus_config_t::mosi_io_num`.
    pub mosi: i32,
    /// Controller in, `spi_bus_config_t::miso_io_num`. [`super::pins::NC`] for
    /// the write-only panel.
    pub miso: i32,
    /// Chip select, `spi_device_interface_config_t::spics_io_num`.
    pub cs: i32,
    /// `spi_device_interface_config_t::clock_speed_hz`.
    pub clock_hz: u32,
    /// `spi_device_interface_config_t::mode`.
    pub mode: SpiMode,
    /// Largest single transfer the bus must carry, `spi_bus_config_t::max_transfer_sz`.
    pub max_transfer_sz: usize,
}

/// Bytes in one full ePaper frame plus its leading command byte.
///
/// 600 x 448 pixels at 4 bits each is 134 400 bytes; the `acep-5in65-7c` driver
/// prefixes the `DATA_START_TRANSMISSION` opcode.
pub const EINK_FRAME_BYTES: usize = 600 * 448 / 2 + 1;

/// Largest transfer the ePaper bus is configured for.
///
/// **Deviation from C, and the one thing on this page worth a second look.**
/// `acep_5in65_7c.c:181` set `max_transfer_sz = 8`, because that driver clocked
/// the framebuffer out one byte at a time in a loop (C:290-296). The Rust
/// `acep-5in65-7c` crate sends the frame as a single transfer instead -- same
/// MOSI byte stream, one CS assertion -- so the bus limit has to cover it.
///
/// 4096 is a DMA-safe chunk rather than the full [`EINK_FRAME_BYTES`]:
/// `esp-idf-hal`'s `SpiDeviceDriver` splits a write longer than the configured
/// maximum into chunks itself, and asking ESP-IDF to reserve DMA descriptors
/// for a 134 kB single transfer is both unnecessary and larger than the S3's
/// per-transfer limit.
///
/// UNVERIFIED on hardware: if the panel turns out to need one unbroken CS
/// assertion for the whole frame, this is the constant to raise, and the chunk
/// boundaries are the thing to suspect.
pub const EINK_MAX_TRANSFER_SZ: usize = 4096;

// C's max_transfer_sz was 8. Asserted at compile time rather than in a #[test]
// because clippy rejects a runtime assertion on constants.
const _: () = assert!(EINK_MAX_TRANSFER_SZ > 8);
const _: () = assert!(EINK_FRAME_BYTES > EINK_MAX_TRANSFER_SZ);

/// The ePaper bus, for the board being built.
pub const EINK_BUS: SpiBus = eink_bus(PINS);

/// The ePaper bus for an arbitrary pin map, so both boards can be tested.
#[must_use]
pub const fn eink_bus(pins: PinMap) -> SpiBus {
    SpiBus {
        // src/esp32/gui.c:69
        host: SpiHost::Spi3,
        sclk: pins.eink_spi_clk,
        mosi: pins.eink_spi_mosi,
        miso: pins.eink_spi_miso,
        cs: pins.eink_spi_ncs,
        // acep_5in65_7c.c:183 -- "Clock out at 1 MHz"
        clock_hz: 1_000_000,
        // acep_5in65_7c.c:184
        mode: SpiMode::Mode0,
        max_transfer_sz: EINK_MAX_TRANSFER_SZ,
    }
}

/// Data-bus width of an SDMMC slot.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SdWidth {
    /// One data line.
    Bits1,
    /// Four data lines, `slot_config.width = 4`.
    Bits4,
    /// `slot_config.width = 0`: let the driver use the widest the slot
    /// supports. What `SDMMC_SLOT_CONFIG_DEFAULT()` leaves it at on the REV2
    /// board.
    DriverDefault,
}

/// The SD card bus: an SDMMC slot, see the module docs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SdBus {
    /// SDMMC CLK, `SD_SPI_CLK`.
    pub clk: i32,
    /// SDMMC CMD. `include/pins.h` calls it `SD_SPI_nCS`.
    pub cmd: i32,
    /// SDMMC D0, `SD_SPI_D0`.
    pub d0: i32,
    /// SDMMC D1, `SD_SPI_D1`.
    pub d1: i32,
    /// SDMMC D2, `SD_SPI_D2`.
    pub d2: i32,
    /// SDMMC D3, `SD_SPI_D3`.
    pub d3: i32,
    /// Card detect handed to the SDMMC driver, `slot_config.cd`.
    ///
    /// `None` is `SDMMC_SLOT_NO_CD`, which is what
    /// `SDMMC_SLOT_CONFIG_DEFAULT()` gives the REV2 board. Both boards
    /// *additionally* poll `SD_CARD_nDET` as a plain GPIO
    /// (src/esp32/sd.c:267, :271) -- that is the mount/unmount trigger, and it
    /// is independent of this field.
    pub cd: Option<i32>,
    /// Write protect, `slot_config.wp`. `None` is `SDMMC_SLOT_NO_WP`; neither
    /// board wires one.
    pub wp: Option<i32>,
    /// `slot_config.width`.
    pub width: SdWidth,
}

/// The SD bus, for the board being built.
pub const SD_BUS: SdBus = sd_bus(PINS, cfg!(feature = "board-esp32s3"));

/// The SD bus for an arbitrary pin map, so both boards can be tested.
///
/// The `esp32s3` flag is not redundant with `pins`: `src/esp32/sd.c` spells the
/// slot out by hand under `#ifdef ESP_S3` and falls back to
/// `SDMMC_SLOT_CONFIG_DEFAULT()` otherwise, and the two differ in `cd` and
/// `width` even though the pin numbers agree.
#[must_use]
pub const fn sd_bus(pins: PinMap, esp32s3: bool) -> SdBus {
    SdBus {
        clk: pins.sd_spi_clk,
        cmd: pins.sd_spi_ncs,
        d0: pins.sd_spi_d0,
        d1: pins.sd_spi_d1,
        d2: pins.sd_spi_d2,
        d3: pins.sd_spi_d3,
        // src/esp32/sd.c:47 on S3; SDMMC_SLOT_NO_CD via the default macro
        // otherwise.
        cd: if esp32s3 {
            Some(pins.sd_card_ndet)
        } else {
            None
        },
        wp: None,
        // src/esp32/sd.c:49 sets 4 explicitly; the default macro leaves 0.
        width: if esp32s3 {
            SdWidth::Bits4
        } else {
            SdWidth::DriverDefault
        },
    }
}

/// `esp-idf-hal` construction of the ePaper SPI device.
#[cfg(target_os = "espidf")]
pub use espidf::*;

#[cfg(target_os = "espidf")]
mod espidf {
    //! UNVERIFIED BY A COMPILER -- see rust/PORTING.md "Firmware build".

    use esp_idf_hal::gpio::{AnyIOPin, AnyOutputPin, OutputPin};
    use esp_idf_hal::peripheral::Peripheral;
    use esp_idf_hal::spi::config::{Config, DriverConfig};
    use esp_idf_hal::spi::{Dma, SpiAnyPins, SpiDeviceDriver, SpiDriver};
    use esp_idf_hal::sys::EspError;
    use esp_idf_hal::units::Hertz;

    use super::{EINK_BUS, EINK_MAX_TRANSFER_SZ};

    /// The panel's SPI device.
    ///
    /// `SpiDeviceDriver` implements `embedded_hal::spi::SpiDevice`, which is
    /// what `acep_5in65_7c::Acep5In65::new` takes -- so the driver never learns
    /// which SPI peripheral, or which chip, it is talking to. That is the trait
    /// boundary rust/PORTING.md describes, and it is why the panel driver is
    /// unit-tested on the host against a recording fake.
    pub type EinkSpi<'d> = SpiDeviceDriver<'d, SpiDriver<'d>>;

    /// Bring up the ePaper SPI bus and attach the panel to it.
    ///
    /// Replaces `spi_bus_initialize` + `spi_bus_add_device` in
    /// `acep_5in65_7c.c:191-197`, with two deliberate differences:
    ///
    /// - **No `pre_cb`.** C used a pre-transfer callback to drive `EINK_DC`
    ///   (`acep_5in65_7c.c:162-166, 188`). The Rust driver owns `EINK_DC` as an
    ///   `OutputPin` and sets it around each command, so there is no callback
    ///   and no global `dev` pointer for it to read.
    /// - **`max_transfer_sz`** is [`EINK_MAX_TRANSFER_SZ`], not C's 8; see that
    ///   constant for why.
    ///
    /// `EINK_DC` and `EINK_BUSY` are *not* configured here -- they are not bus
    /// pins, and the panel driver takes them as `embedded-hal` pins. Use
    /// [`super::super::gpio::output`] and [`super::super::gpio::input`].
    pub fn eink_spi<'d, SPI: SpiAnyPins>(
        spi: impl Peripheral<P = SPI> + 'd,
        sclk: impl Peripheral<P = impl OutputPin> + 'd,
        mosi: impl Peripheral<P = impl OutputPin> + 'd,
        cs: impl Peripheral<P = AnyOutputPin> + 'd,
    ) -> Result<EinkSpi<'d>, EspError> {
        // miso is EINK_SPI_MISO == NC on both boards: the panel is write-only.
        debug_assert_eq!(EINK_BUS.miso, super::super::pins::NC);
        let driver = SpiDriver::new::<SPI>(
            spi,
            sclk,
            mosi,
            None::<AnyIOPin>,
            &DriverConfig {
                dma: Dma::Auto(EINK_MAX_TRANSFER_SZ),
                ..Default::default()
            },
        )?;
        let config = Config::new()
            .baudrate(Hertz(EINK_BUS.clock_hz))
            .data_mode(esp_idf_hal::spi::config::MODE_0);
        SpiDeviceDriver::new(driver, Some(cs), &config)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::pins::{ESP32S3, ESP32_REV2, NC};

    #[test]
    fn eink_bus_matches_the_c_driver_on_esp32s3() {
        let b = eink_bus(ESP32S3);
        assert_eq!(b.host, SpiHost::Spi3, "src/esp32/gui.c:69");
        assert_eq!(b.sclk, 39);
        assert_eq!(b.mosi, 13);
        assert_eq!(b.miso, NC);
        assert_eq!(b.cs, 38);
        assert_eq!(b.clock_hz, 1_000_000, "acep_5in65_7c.c:183");
        assert_eq!(b.mode, SpiMode::Mode0, "acep_5in65_7c.c:184");
    }

    #[test]
    fn eink_bus_matches_the_c_driver_on_esp32_rev2() {
        let b = eink_bus(ESP32_REV2);
        assert_eq!(b.host, SpiHost::Spi3);
        assert_eq!(b.sclk, 18);
        assert_eq!(b.mosi, 17);
        assert_eq!(b.miso, NC);
        assert_eq!(b.cs, 5);
        // The bus parameters are the panel's, not the board's.
        assert_eq!(b.clock_hz, 1_000_000);
        assert_eq!(b.mode, SpiMode::Mode0);
    }

    #[test]
    fn eink_max_transfer_is_big_enough_for_a_chunked_frame_flush() {
        // 600 x 448 at 4 bpp, plus the DATA_START_TRANSMISSION opcode.
        assert_eq!(EINK_FRAME_BYTES, 134_401);
        assert_eq!(EINK_MAX_TRANSFER_SZ, 4096);
        assert_eq!(EINK_BUS.max_transfer_sz, EINK_MAX_TRANSFER_SZ);
    }

    #[test]
    fn sd_bus_matches_sd_c_on_esp32s3() {
        let b = sd_bus(ESP32S3, true);
        assert_eq!(b.clk, 19);
        assert_eq!(b.cmd, 47, "SD_SPI_nCS is SDMMC CMD");
        assert_eq!(b.d0, 48);
        assert_eq!(b.d1, 21);
        assert_eq!(b.d2, 20);
        assert_eq!(b.d3, 9);
        assert_eq!(b.cd, Some(4), "src/esp32/sd.c:47");
        assert_eq!(b.wp, None, "SDMMC_SLOT_NO_WP");
        assert_eq!(b.width, SdWidth::Bits4, "src/esp32/sd.c:49");
    }

    #[test]
    fn sd_bus_matches_the_slot_defaults_on_esp32_rev2() {
        let b = sd_bus(ESP32_REV2, false);
        // The REV2 board is wired to the ESP32's fixed SDMMC slot-1 pins,
        // which is why sd.c can get away with SDMMC_SLOT_CONFIG_DEFAULT().
        assert_eq!(b.clk, 14);
        assert_eq!(b.cmd, 15);
        assert_eq!(b.d0, 2);
        assert_eq!(b.d1, 4);
        assert_eq!(b.d2, 12);
        assert_eq!(b.d3, 13);
        assert_eq!(b.cd, None, "SDMMC_SLOT_CONFIG_DEFAULT leaves no CD");
        assert_eq!(b.wp, None);
        assert_eq!(b.width, SdWidth::DriverDefault);
    }

    #[test]
    fn the_active_board_descriptors_come_from_the_active_pin_map() {
        assert_eq!(EINK_BUS, eink_bus(PINS));
        assert_eq!(SD_BUS, sd_bus(PINS, cfg!(feature = "board-esp32s3")));
    }

    #[test]
    fn the_two_buses_share_no_pin() {
        for (pins, s3) in [(ESP32S3, true), (ESP32_REV2, false)] {
            let e = eink_bus(pins);
            let s = sd_bus(pins, s3);
            let eink = [e.sclk, e.mosi, e.cs];
            let sd = [s.clk, s.cmd, s.d0, s.d1, s.d2, s.d3];
            for a in eink {
                assert!(!sd.contains(&a), "pin {a} is on both buses");
            }
        }
    }
}
