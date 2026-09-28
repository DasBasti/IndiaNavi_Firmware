//! Battery gauge and charger detection -- the pure half of `readBatteryPercent`.
//!
//! Replaces: the body of `readBatteryPercent` in src/esp32/main.c:121-159
//!
//! The C function did three things at once: two ADC reads, a charger-state
//! edge detection that pushed an event onto `eventQueueHandle`, and a
//! voltage-to-percent ramp. Only the middle and last parts are logic, so only
//! those live here, as [`evaluate`]. The ADC reads are in
//! [`super::adc`], and pushing the event onto the queue stays in the power
//! task -- which is what makes this function testable on the host.
//!
//! Note the C name is a lie twice over: it returns a percentage, not a
//! voltage, and the numbers it works with are raw 12-bit ADC counts, not
//! millivolts. `adc_oneshot_read` is never passed through
//! `adc_cali_raw_to_voltage`, so `min`/`max` below are counts. The names here
//! say `raw` to stop the next reader making the same mistake.

/// Raw ADC count treated as an empty battery, `readBatteryPercent`'s `min`
/// (src/esp32/main.c:147).
pub const EMPTY_RAW: i32 = 1550;

/// Raw ADC count treated as a full battery, `readBatteryPercent`'s `max`
/// (src/esp32/main.c:148).
pub const FULL_RAW: i32 = 2330;

/// How far `VIN_ADC` must exceed `VBAT_ADC` before the charger counts as
/// present, the literal `5` in src/esp32/main.c:132 and :138.
///
/// The same constant on both comparisons gives a dead band rather than
/// hysteresis; see [`evaluate`].
pub const CHARGER_MARGIN_RAW: i32 = 5;

/// What the charger state just did.
///
/// Replaces the `TASK_EVENT_START_CHARGING` / `TASK_EVENT_STOP_CHARGING`
/// values `readBatteryPercent` pushed onto `eventQueueHandle`
/// (src/esp32/main.c:133-141). The caller decides what to do with it, which in
/// the C tree meant starting or killing the WiFi task
/// (src/esp32/main.c:382-386).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChargeEvent {
    /// The charger appeared: `TASK_EVENT_START_CHARGING`.
    Started,
    /// The charger went away: `TASK_EVENT_STOP_CHARGING`.
    Stopped,
}

/// The result of one gauge poll.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BatteryStatus {
    /// State of charge, 0..=100. `readBatteryPercent`'s return value.
    pub percent: u8,
    /// Whether the charger is currently considered present. The C tree kept
    /// this in the global `is_charging` (src/esp32/main.c:77).
    pub charging: bool,
    /// Set only on the poll where `charging` flipped, so the caller can send
    /// one event per edge, as C did.
    pub event: Option<ChargeEvent>,
}

/// Map a raw `VBAT_ADC` count onto 0..=100 %.
///
/// Replaces src/esp32/main.c:147-158, clamps included. The C comment calls it
/// "ganz simpler dreisatz" -- a linear ramp between [`EMPTY_RAW`] and
/// [`FULL_RAW`] with truncating integer division, reproduced exactly -- so a reading one count
/// above [`EMPTY_RAW`] still reports 0 %, and it takes 8 counts to reach 1 %.
#[must_use]
pub const fn percent_from_raw(battery_raw: i32) -> u8 {
    if battery_raw > FULL_RAW {
        return 100;
    }
    if battery_raw < EMPTY_RAW {
        return 0;
    }
    // Both bounds are compile-time constants with FULL_RAW > EMPTY_RAW, so the
    // divisor is a non-zero positive number and the result is in 0..=100.
    let value = battery_raw - EMPTY_RAW;
    (value * 100 / (FULL_RAW - EMPTY_RAW)) as u8
}

/// One poll of the gauge: percentage, charger state, and the edge if there was
/// one.
///
/// Replaces `readBatteryPercent` (src/esp32/main.c:121-159) minus its two
/// `adc_oneshot_read` calls and its `xQueueSend`. `was_charging` is the
/// previous value of the C global `is_charging`; the returned
/// [`BatteryStatus::charging`] is the new one.
///
/// The charger test is C's, verbatim, including its dead band: the charger is
/// latched on when `charger_raw - 5 > battery_raw` and off when
/// `charger_raw - 5 < battery_raw`, so the single count where
/// `charger_raw - 5 == battery_raw` changes nothing and the previous state
/// persists. That is deliberate in effect if not in intent -- it stops the
/// state chattering when the two rails sit on top of each other, which is
/// exactly what happens on a full battery with the charger still plugged in.
#[must_use]
pub const fn evaluate(battery_raw: i32, charger_raw: i32, was_charging: bool) -> BatteryStatus {
    // `saturating_sub` where C wrote `chargerVoltage - 5`: the C subtraction
    // is undefined behaviour at INT_MIN and Rust panics on it in a debug
    // build. Saturating keeps an absurd reading meaning "no charger".
    let threshold = charger_raw.saturating_sub(CHARGER_MARGIN_RAW);
    let (charging, event) = if threshold > battery_raw && !was_charging {
        (true, Some(ChargeEvent::Started))
    } else if threshold < battery_raw && was_charging {
        (false, Some(ChargeEvent::Stopped))
    } else {
        (was_charging, None)
    };

    BatteryStatus {
        percent: percent_from_raw(battery_raw),
        charging,
        event,
    }
}

/// How long the power task should wait before polling again.
///
/// Replaces src/esp32/main.c:478-482: 1 s while charging, 60 s otherwise.
/// Returned in milliseconds, the unit the C code fed to `pdMS_TO_TICKS`.
#[must_use]
pub const fn poll_interval_ms(charging: bool) -> u32 {
    if charging {
        1_000
    } else {
        60_000
    }
}

/// Battery level the GUI task refuses to start the ePaper panel below.
///
/// From src/esp32/gui.c:352 (`while (current_battery_level < 65)`). Lives here
/// next to the gauge it compares against; the GUI task is the one that waits.
pub const EPAPER_MIN_PERCENT: u8 = 65;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_battery_reads_zero() {
        // Exactly at `min` the ramp starts, so it is 0 rather than clamped.
        assert_eq!(percent_from_raw(EMPTY_RAW), 0);
        // Below `min` the C code returns early.
        assert_eq!(percent_from_raw(EMPTY_RAW - 1), 0);
        assert_eq!(percent_from_raw(0), 0);
        // A disconnected or shorted sense line must not wrap or panic.
        assert_eq!(percent_from_raw(i32::MIN), 0);
    }

    #[test]
    fn full_battery_reads_one_hundred() {
        // `> max` is the C condition, so `max` itself goes through the ramp and
        // lands on 100 anyway: 780 * 100 / 780.
        assert_eq!(percent_from_raw(FULL_RAW), 100);
        assert_eq!(percent_from_raw(FULL_RAW + 1), 100);
        assert_eq!(percent_from_raw(i32::MAX), 100);
    }

    #[test]
    fn mid_battery_follows_the_c_integer_ramp() {
        // Midpoint: (1940 - 1550) * 100 / 780 = 39000 / 780 = 50.
        assert_eq!(percent_from_raw(1940), 50);
        // Truncation, not rounding: (1943 - 1550) * 100 / 780 = 39300 / 780
        // = 50.38 -> 50.
        assert_eq!(percent_from_raw(1943), 50);
        // A quarter: (1745 - 1550) * 100 / 780 = 19500 / 780 = 25.
        assert_eq!(percent_from_raw(1745), 25);
        // One count above empty is still 0 after truncation.
        assert_eq!(percent_from_raw(EMPTY_RAW + 1), 0);
        // First count that reaches 1 %: needs value >= 7.8, i.e. 8.
        assert_eq!(percent_from_raw(EMPTY_RAW + 7), 0);
        assert_eq!(percent_from_raw(EMPTY_RAW + 8), 1);
        // One below full.
        assert_eq!(percent_from_raw(FULL_RAW - 8), 98);
    }

    #[test]
    fn the_ramp_never_leaves_zero_to_one_hundred() {
        let mut previous = 0;
        for raw in (EMPTY_RAW - 10)..=(FULL_RAW + 10) {
            let p = percent_from_raw(raw);
            assert!(p <= 100, "raw {raw} gave {p}");
            assert!(p >= previous, "raw {raw} went backwards: {previous} -> {p}");
            previous = p;
        }
        assert_eq!(previous, 100);
    }

    #[test]
    fn charger_appearing_raises_exactly_one_started_event() {
        // VIN well above VBAT, and we were not charging.
        let s = evaluate(1800, 2400, false);
        assert!(s.charging);
        assert_eq!(s.event, Some(ChargeEvent::Started));

        // Polling again with the same readings is silent: the edge is gone.
        let s = evaluate(1800, 2400, s.charging);
        assert!(s.charging);
        assert_eq!(s.event, None);
    }

    #[test]
    fn charger_going_away_raises_exactly_one_stopped_event() {
        let s = evaluate(1800, 1000, true);
        assert!(!s.charging);
        assert_eq!(s.event, Some(ChargeEvent::Stopped));

        let s = evaluate(1800, 1000, s.charging);
        assert!(!s.charging);
        assert_eq!(s.event, None);
    }

    #[test]
    fn charging_state_does_not_affect_the_reported_percentage() {
        assert_eq!(evaluate(1940, 2400, false).percent, 50);
        assert_eq!(evaluate(1940, 1000, true).percent, 50);
    }

    #[test]
    fn the_margin_is_a_dead_band_and_holds_the_previous_state() {
        // charger_raw - 5 == battery_raw: neither C branch is taken.
        assert!(!evaluate(1800, 1805, false).charging);
        assert_eq!(evaluate(1800, 1805, false).event, None);
        assert!(evaluate(1800, 1805, true).charging);
        assert_eq!(evaluate(1800, 1805, true).event, None);
    }

    #[test]
    fn the_margin_boundaries_are_the_c_ones() {
        // Just inside: 1806 - 5 = 1801 > 1800 -> charging starts.
        assert_eq!(
            evaluate(1800, 1806, false).event,
            Some(ChargeEvent::Started)
        );
        // Just outside: 1804 - 5 = 1799 < 1800 -> charging stops.
        assert_eq!(evaluate(1800, 1804, true).event, Some(ChargeEvent::Stopped));
    }

    #[test]
    fn a_full_battery_on_the_charger_reports_full_and_charging() {
        let s = evaluate(FULL_RAW, FULL_RAW + 100, false);
        assert_eq!(s.percent, 100);
        assert!(s.charging);
        assert_eq!(s.event, Some(ChargeEvent::Started));
    }

    #[test]
    fn an_empty_battery_on_the_charger_reports_zero_and_charging() {
        let s = evaluate(EMPTY_RAW - 100, 2000, false);
        assert_eq!(s.percent, 0);
        assert!(s.charging);
        assert_eq!(s.event, Some(ChargeEvent::Started));
    }

    #[test]
    fn extreme_readings_do_not_overflow_the_margin_subtraction() {
        // `charger_raw - 5` must not wrap. i32::MIN - 5 would.
        let s = evaluate(0, i32::MIN, false);
        assert!(!s.charging);
        let s = evaluate(0, i32::MAX, false);
        assert!(s.charging);
    }

    #[test]
    fn poll_interval_matches_the_power_task() {
        assert_eq!(poll_interval_ms(true), 1_000);
        assert_eq!(poll_interval_ms(false), 60_000);
    }
}
