//! ADC1 one-shot setup for the battery and charger sense lines.
//!
//! Replaces: the `adc_oneshot_*` setup in `StartPowerTask`
//! (src/esp32/main.c:459-472) and the two `adc_oneshot_read` calls in
//! `readBatteryPercent` (src/esp32/main.c:127-129)
//!
//! # `VBAT_ADC` and `VIN_ADC` are channel numbers, not GPIOs
//!
//! This trips everyone once. `include/pins.h` puts `VBAT_ADC` and `VIN_ADC` in
//! the same block as the GPIO numbers, but `src/esp32/main.c` passes them to
//! `adc_oneshot_config_channel` and `adc_oneshot_read`, both of which take an
//! `adc_channel_t`. So they are ADC1 channel indices, and the GPIO they
//! correspond to is fixed by silicon:
//!
//! | Board | `VBAT_ADC` | GPIO | `VIN_ADC` | GPIO |
//! | --- | --- | --- | --- | --- |
//! | ESP32-S3 | ADC1 ch 5 | GPIO6 | ADC1 ch 6 | GPIO7 |
//! | ESP32 REV2 | ADC1 ch 6 | GPIO34 | ADC1 ch 7 | GPIO35 |
//!
//! `esp-idf-hal`'s one-shot driver is typed on the GPIO, not the channel, which
//! is why the mapping has to be written down rather than passed through. The
//! constants below are the mapping; the host tests pin it, and
//! [`Sense::gpio`] is what the device-side code uses.
//!
//! # Calibration: there isn't any
//!
//! The C code never calls `adc_cali_raw_to_voltage`, so every number that
//! reaches [`super::battery`] is a raw count at 12 dB attenuation. The port
//! keeps it that way -- introducing calibration would move the
//! [`super::battery::EMPTY_RAW`]/[`super::battery::FULL_RAW`] thresholds and
//! silently change the gauge. That is its own task, on hardware.

use super::pins::{PinMap, PINS};

/// Which sense line to read.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Sense {
    /// Battery voltage divider, `VBAT_ADC`.
    Battery,
    /// Charger / USB input voltage divider, `VIN_ADC`.
    ChargerInput,
}

impl Sense {
    /// ADC1 channel index, straight out of `include/pins.h` for the board being
    /// built.
    #[must_use]
    pub const fn channel(self) -> i32 {
        Self::channel_on(self, PINS)
    }

    /// ADC1 channel index on an arbitrary board, so both tables can be tested.
    #[must_use]
    pub const fn channel_on(self, pins: PinMap) -> i32 {
        match self {
            Sense::Battery => pins.vbat_adc,
            Sense::ChargerInput => pins.vin_adc,
        }
    }

    /// GPIO the channel is wired to on the board being built.
    ///
    /// See the module docs: this is silicon, not a board choice.
    #[must_use]
    pub const fn gpio(self) -> i32 {
        Self::gpio_on(self, cfg!(feature = "board-esp32s3"))
    }

    /// GPIO for a channel, given whether the target is an ESP32-S3.
    #[must_use]
    pub const fn gpio_on(self, esp32s3: bool) -> i32 {
        match (esp32s3, self) {
            // ESP32-S3 TRM: ADC1 channels 0..9 are GPIO1..GPIO10, so ch N is
            // GPIO N+1.
            (true, Sense::Battery) => 6,
            (true, Sense::ChargerInput) => 7,
            // ESP32 TRM: ADC1 channels 0..3 are GPIO36,37,38,39 and channels
            // 4..7 are GPIO32..GPIO35, so ch 6 is GPIO34 and ch 7 is GPIO35.
            (false, Sense::Battery) => 34,
            (false, Sense::ChargerInput) => 35,
        }
    }
}

/// ADC unit both channels live on: `ADC_UNIT_1` (src/esp32/main.c:461).
pub const UNIT: u8 = 1;

/// Attenuation both channels are configured with: `ADC_ATTEN_DB_12`
/// (src/esp32/main.c:467).
///
/// Commit 99a24fb moved this off the deprecated `ADC_ATTEN_DB_11`, so 12 dB is
/// current, not a typo. The value is the ESP-IDF `adc_atten_t` discriminant.
pub const ATTENUATION_DB: u8 = 12;

/// One pair of readings, in raw counts.
///
/// The two fields are in the order `readBatteryPercent` reads them
/// (src/esp32/main.c:127-129), and are what [`super::battery::evaluate`] takes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SenseReading {
    /// Raw `VBAT_ADC` count.
    pub battery_raw: i32,
    /// Raw `VIN_ADC` count.
    pub charger_raw: i32,
}

/// The device-side one-shot driver.
#[cfg(target_os = "espidf")]
pub use espidf::*;

#[cfg(target_os = "espidf")]
mod espidf {
    //! UNVERIFIED BY A COMPILER -- see rust/PORTING.md "Firmware build". The
    //! channel numbers, the unit and the attenuation above *are* covered by
    //! host tests; only this construction is not. The API it is written
    //! against is `esp-idf-hal` 0.47.0's `adc::oneshot`, checked against that
    //! crate's published source and its `examples/adc_oneshot.rs`.
    //!
    //! The generics are fiddly and worth spelling out, because the one-shot
    //! driver is typed three ways at once:
    //!
    //! - `AdcDriver<'d, U>` is generic over the ADC **unit** marker (`ADCU1`),
    //!   not over the `ADC1` peripheral you hand to `AdcDriver::new`.
    //! - `AdcChannelDriver<'d, C, M>` is generic over the **channel** marker
    //!   (`ADCCH5<ADCU1>`), which you get from the pin as
    //!   `<Gpio6 as ADCPin>::AdcChannel` -- not over the pin itself.
    //! - `M` is how the channel borrows the unit; `&'d AdcDriver<..>` is the
    //!   shape that lets both channels share one unit, which is what the C
    //!   power task did with its single `adc1_handle`.

    use esp_idf_hal::adc::attenuation::DB_12;
    use esp_idf_hal::adc::oneshot::config::AdcChannelConfig;
    use esp_idf_hal::adc::oneshot::{AdcChannelDriver, AdcDriver};
    use esp_idf_hal::adc::{ADC1, ADCU1};
    use esp_idf_hal::gpio::ADCPin;
    use esp_idf_hal::sys::EspError;

    use super::SenseReading;

    /// GPIO carrying `VBAT_ADC` on this board. See the module docs for why this
    /// is not `PINS.vbat_adc`.
    #[cfg(feature = "board-esp32s3")]
    pub type VbatPin<'d> = esp_idf_hal::gpio::Gpio6<'d>;
    /// GPIO carrying `VIN_ADC` on this board.
    #[cfg(feature = "board-esp32s3")]
    pub type VinPin<'d> = esp_idf_hal::gpio::Gpio7<'d>;

    #[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
    pub type VbatPin<'d> = esp_idf_hal::gpio::Gpio34<'d>;
    #[cfg(all(feature = "board-esp32", not(feature = "board-esp32s3")))]
    pub type VinPin<'d> = esp_idf_hal::gpio::Gpio35<'d>;

    /// The ADC1 one-shot unit, `StartPowerTask`'s `adc1_handle`
    /// (src/esp32/main.c:459-464).
    pub type SenseAdcUnit<'d> = AdcDriver<'d, ADCU1>;

    /// One configured channel on [`SenseAdcUnit`], borrowing the unit.
    type SenseChannel<'d, P> =
        AdcChannelDriver<'d, <P as ADCPin>::AdcChannel, &'d SenseAdcUnit<'d>>;

    /// Both sense channels on ADC1, configured once and read on demand.
    ///
    /// Replaces the `adc1_handle` plus the two `adc_oneshot_config_channel`
    /// calls that `StartPowerTask` made (src/esp32/main.c:459-472). The C code
    /// kept the handle in a task-local and read through it from
    /// `readBatteryPercent`; here the two channel drivers own the
    /// configuration, so a read cannot happen against an unconfigured channel.
    pub struct SenseAdc<'d> {
        battery: SenseChannel<'d, VbatPin<'d>>,
        charger: SenseChannel<'d, VinPin<'d>>,
    }

    impl<'d> SenseAdc<'d> {
        /// Claim ADC1.
        ///
        /// Split out from [`SenseAdc::new`] because both channel drivers borrow
        /// the unit driver, so the caller has to own it. This is the one-shot
        /// equivalent of the C `adc1_handle` living in the power task's stack
        /// frame (src/esp32/main.c:459).
        pub fn unit(adc1: ADC1<'d>) -> Result<SenseAdcUnit<'d>, EspError> {
            AdcDriver::new(adc1)
        }

        /// Configure both channels at 12 dB attenuation and the driver default
        /// bit width, as src/esp32/main.c:465-472 does.
        pub fn new(
            adc: &'d SenseAdcUnit<'d>,
            vbat: VbatPin<'d>,
            vin: VinPin<'d>,
        ) -> Result<Self, EspError> {
            let config = AdcChannelConfig {
                attenuation: DB_12,
                ..Default::default()
            };
            Ok(Self {
                battery: AdcChannelDriver::new(adc, vbat, &config)?,
                charger: AdcChannelDriver::new(adc, vin, &config)?,
            })
        }

        /// Read both channels, battery first, as `readBatteryPercent` did.
        ///
        /// `read_raw`, not `read`: the C code never calibrated, so
        /// [`super::super::battery`]'s thresholds are raw counts. See the module
        /// docs.
        ///
        /// Deviation: C wrapped both reads in `ESP_ERROR_CHECK`, which aborts
        /// the firmware on a failed ADC read. A dropped sample is not worth a
        /// reboot, so this returns the error and the power task can keep the
        /// previous reading.
        pub fn read(&mut self) -> Result<SenseReading, EspError> {
            Ok(SenseReading {
                battery_raw: i32::from(self.battery.read_raw()?),
                charger_raw: i32::from(self.charger.read_raw()?),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::pins::{ESP32S3, ESP32_REV2};

    #[test]
    fn channels_match_pins_h_on_both_boards() {
        assert_eq!(Sense::Battery.channel_on(ESP32S3), 5);
        assert_eq!(Sense::ChargerInput.channel_on(ESP32S3), 6);
        assert_eq!(Sense::Battery.channel_on(ESP32_REV2), 6);
        assert_eq!(Sense::ChargerInput.channel_on(ESP32_REV2), 7);
    }

    #[test]
    fn the_active_board_channels_come_from_the_active_pin_map() {
        assert_eq!(Sense::Battery.channel(), PINS.vbat_adc);
        assert_eq!(Sense::ChargerInput.channel(), PINS.vin_adc);
    }

    #[test]
    fn channel_to_gpio_mapping_is_the_silicon_one() {
        // ESP32-S3: ADC1 ch N -> GPIO N+1.
        assert_eq!(Sense::Battery.gpio_on(true), 6);
        assert_eq!(Sense::ChargerInput.gpio_on(true), 7);
        assert_eq!(
            Sense::Battery.gpio_on(true),
            Sense::Battery.channel_on(ESP32S3) + 1
        );
        assert_eq!(
            Sense::ChargerInput.gpio_on(true),
            Sense::ChargerInput.channel_on(ESP32S3) + 1
        );

        // ESP32: ADC1 ch 4..7 -> GPIO32..GPIO35, i.e. GPIO = ch + 28.
        assert_eq!(Sense::Battery.gpio_on(false), 34);
        assert_eq!(Sense::ChargerInput.gpio_on(false), 35);
        assert_eq!(
            Sense::Battery.gpio_on(false),
            Sense::Battery.channel_on(ESP32_REV2) + 28
        );
        assert_eq!(
            Sense::ChargerInput.gpio_on(false),
            Sense::ChargerInput.channel_on(ESP32_REV2) + 28
        );
    }

    #[test]
    fn the_two_sense_lines_never_share_a_channel_or_a_gpio() {
        for pins in [ESP32S3, ESP32_REV2] {
            assert_ne!(
                Sense::Battery.channel_on(pins),
                Sense::ChargerInput.channel_on(pins)
            );
        }
        for s3 in [true, false] {
            assert_ne!(Sense::Battery.gpio_on(s3), Sense::ChargerInput.gpio_on(s3));
        }
    }

    #[test]
    fn unit_and_attenuation_match_the_power_task() {
        assert_eq!(UNIT, 1, "ADC_UNIT_1");
        assert_eq!(ATTENUATION_DB, 12, "ADC_ATTEN_DB_12, see commit 99a24fb");
    }

    #[test]
    fn the_active_gpio_helper_agrees_with_the_active_board() {
        assert_eq!(
            Sense::Battery.gpio(),
            Sense::Battery.gpio_on(cfg!(feature = "board-esp32s3"))
        );
    }
}
