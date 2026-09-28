//! Shared application state.
//!
//! Replaces: the mutable globals declared in `include/gui.h:35-50` and defined
//! in `src/esp32/gui.c:50-61`, `src/esp32/main.c:76-77` and
//! `src/esp32/wifi.c:37`:
//!
//! | C global | Defined | Rust |
//! | --- | --- | --- |
//! | `map_position_t* map_position` | `src/esp32/gui.c:61` | [`AppState::position`] |
//! | `int32_t current_battery_level` | `src/esp32/main.c:76` | [`BatteryState::level`] |
//! | `int32_t is_charging` | `src/esp32/main.c:77` | [`BatteryState::charging`] |
//! | `label_t* clock_label` | `src/esp32/gui.c:50` | [`Indicators::clock`] |
//! | `label_t* north_indicator_label` | `src/esp32/gui.c:52` | [`Indicators::north`] |
//! | `label_t* gps_indicator_label` | `src/esp32/gui.c:53` | [`Indicators::gps_satellites`] |
//! | `label_t* sd_indicator_label` | `src/esp32/gui.c:54` | [`Indicators::sd`] |
//! | `label_t* wifi_indicator_label` | declared `include/gui.h:37`, never defined | [`Indicators::wifi`] |
//! | `battery_indicator_t* battery_indicator` | `src/esp32/gui.c:51` | [`AppState::battery`] |
//! | `uint8_t* wifi_indicator_image_data` | `src/esp32/wifi.c:37` | [`Indicators::wifi`] |
//!
//! # Data, not widgets
//!
//! Seven of those are `label_t*`/`image_t*` pointers into GUI objects owned by
//! the GUI task, published globally so that *other* tasks can reach in and
//! write them: `src/esp32/sd.c:65` swaps the SD icon's pixel data from the SD
//! task, `src/esp32/wifi.c:219-227` swaps the WiFi icon's from the WiFi task,
//! and `src/screens/map_screen.c:107-113` rewrites the GPS label's text and
//! icon. Every one of those is a cross-task write to memory the GUI task may
//! be rendering from at that instant.
//!
//! This module publishes the *inputs* to those widgets instead — a satellite
//! count, an RSSI bucket, a mount status. The GUI task keeps sole ownership of
//! every `label_t`/`image_t` and folds the values in during its own render
//! pass. That deletes the sharing rather than locking it, which is why the
//! indicator fields here are plain `Copy` data and not handles.
//!
//! # One lock, not seven
//!
//! Everything lives behind a single [`SharedState`] lock. Two reasons:
//!
//! - **Consistency.** `src/screens/map_screen.c:59-123` reads `latitude`,
//!   `longitude`, `fix`, `hdop` and `satellites_in_view` in one render. With a
//!   lock per field it could see a position from two different fixes. The C
//!   code avoids that only because `map_position` is swapped as a single
//!   pointer (`src/esp32/gps.c:153`) — but it points at a struct the GPS task
//!   keeps mutating, so the C code does not actually avoid it.
//! - **Cost.** The critical sections here are a handful of field copies. A
//!   `Mutex` that is never held across I/O, a `wait`, or a render cannot
//!   meaningfully contend, so splitting it buys nothing and costs a reader the
//!   ability to take a coherent snapshot.
//!
//! What it must never be used for: holding the lock across a display flush or
//! an SD read. [`SharedState::snapshot`] exists so callers do not have to —
//! take a copy, drop the lock, then render.
//!
//! # Placeholders
//!
//! [`MapPosition`] and [`GpsFix`] are transcriptions of `map_position_t`
//! (`lib/Platinenmacher/gui/map.h:28-37`) and `gps_fix_t`
//! (`lib/Platinenmacher/gps.h:12-17`). Those types belong in `pm-gui` and
//! `pm-core` respectively; neither crate exists on this branch (see the
//! project note about the unmerged scaffold). When they land, delete the
//! definitions here and re-export, exactly as `pm-parser` was told to do with
//! its placeholder `Error`. The field names and value ranges are chosen to
//! match, so no call site should have to change.

use std::sync::{Mutex, MutexGuard, PoisonError};

/// GPS fix quality.
///
/// Replaces `gps_fix_t` (`lib/Platinenmacher/gps.h:12-17`). Discriminants are
/// the C values; note that the C enum jumps from 2 to 6.
///
/// PLACEHOLDER: belongs in `pm_core::gps` — see the module header.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GpsFix {
    /// `GPS_FIX_INVALID` — not fixed.
    #[default]
    Invalid = 0,
    /// `GPS_FIX_GPS`
    Gps = 1,
    /// `GPS_FIX_DGPS` — differential GPS.
    Dgps = 2,
    /// `GPS_FIX_DR` — dead reckoning, still a valid fix.
    DeadReckoning = 6,
}

impl GpsFix {
    /// True for every fix the C code treats as usable, i.e. everything the
    /// `fix != GPS_FIX_INVALID` tests at `src/screens/map_screen.c:65`, `:90`
    /// and `:110` accept.
    pub fn is_valid(self) -> bool {
        self != GpsFix::Invalid
    }

    /// The C `gps_fix_t` value.
    pub const fn as_raw(self) -> u8 {
        self as u8
    }

    /// Recover a fix from a C `gps_fix_t` value. `None` for the undefined
    /// values 3, 4, 5 and 7..=255.
    pub const fn from_raw(raw: u8) -> Option<GpsFix> {
        match raw {
            0 => Some(GpsFix::Invalid),
            1 => Some(GpsFix::Gps),
            2 => Some(GpsFix::Dgps),
            6 => Some(GpsFix::DeadReckoning),
            _ => None,
        }
    }
}

/// The current position.
///
/// Replaces `map_position_t` (`lib/Platinenmacher/gui/map.h:28-37`), published
/// in C as the `map_position` pointer.
///
/// PLACEHOLDER: belongs in `pm_gui::map` — see the module header.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct MapPosition {
    /// Degrees east.
    pub longitude: f32,
    /// Degrees north.
    pub latitude: f32,
    /// Metres.
    pub altitude: f32,
    /// Horizontal dilution of precision.
    pub hdop: f32,
    /// Slippy-map zoom level.
    pub zoom_level: u8,
    /// Fix quality.
    pub fix: GpsFix,
    /// Satellites the receiver can see.
    pub satellites_in_view: u8,
    /// Satellites contributing to the fix.
    pub satellites_in_use: u8,
}

/// Battery level and charge state.
///
/// Replaces `current_battery_level` and `is_charging`
/// (`src/esp32/main.c:76-77`), which are written together by
/// `readBatteryPercent` (`src/esp32/main.c:132-144`) and the power task loop
/// (`src/esp32/main.c:474-476`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct BatteryState {
    /// Percent, 0..=100. `readBatteryPercent` clamps
    /// (`src/esp32/main.c:148-155`), so the port keeps `i32` only to match the
    /// C `int32_t` at the FFI boundary while the C tree still exists.
    pub level: i32,
    /// `is_charging`. `int32_t` in C, used only as a boolean.
    pub charging: bool,
}

/// WiFi signal strength, as the status icon shows it.
///
/// Replaces `wifi_indicator_image_data` (`src/esp32/wifi.c:37`), which the
/// WiFi task points at one of `WIFI_0`..`WIFI_3`
/// (`src/esp32/wifi.c:219-227`). The port publishes the *bucket* and lets the
/// GUI task pick the icon, so no pixel-data pointer crosses a task boundary.
///
/// The thresholds are the C ones: `rssi >= -70` is [`WifiLevel::Strong`],
/// `-80 ..= -71` is [`WifiLevel::Medium`], below `-80` is
/// [`WifiLevel::Weak`].
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Default)]
pub enum WifiLevel {
    /// `WIFI_0` — not connected. The value `wifi_indicator_image_data` is
    /// initialised to.
    #[default]
    Disconnected = 0,
    /// `WIFI_1` — `rssi < -80`.
    Weak = 1,
    /// `WIFI_2` — `-80 <= rssi < -70`.
    Medium = 2,
    /// `WIFI_3` — `rssi >= -70`.
    Strong = 3,
}

impl WifiLevel {
    /// Bucket an RSSI the way `src/esp32/wifi.c:216-227` does.
    pub fn from_rssi(rssi: i8) -> WifiLevel {
        if rssi >= -70 {
            WifiLevel::Strong
        } else if rssi >= -80 {
            WifiLevel::Medium
        } else {
            WifiLevel::Weak
        }
    }
}

/// Whether the SD card mounted.
///
/// Replaces the `sd_status` that `statusRender` (`src/esp32/sd.c:63-71`) tests
/// to choose between the `SD` and `noSD` icons.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SdStatus {
    /// The SD task has not finished mounting yet.
    #[default]
    Unknown,
    /// `sd_status == PM_OK` — the `SD` icon.
    Mounted,
    /// The `noSD` icon.
    Failed,
}

/// The status-bar indicator values.
///
/// Replaces the five `label_t*` indicator globals and
/// `wifi_indicator_image_data`. See the module header for why these are values
/// and not widget handles.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Indicators {
    /// `clock_label->text`. Written by `updateTimeText`
    /// (`src/esp32/gui.c:151-160`) as `"%02d:%02d"`.
    pub clock: String,
    /// `north_indicator_label->text`. Created with an empty string at
    /// `src/esp32/gui.c:193-196`; kept here so the compass port has somewhere
    /// to publish a heading without reintroducing a global.
    pub north: String,
    /// `wifi_indicator_image_data`, as a bucket rather than a pixel pointer.
    pub wifi: WifiLevel,
    /// `gps_indicator_label->text`: the satellite count `updateSatsInView`
    /// writes at `src/screens/map_screen.c:107`. The icon
    /// (`GPS` vs `GPS_lock`) follows from [`MapPosition::fix`], so it is not
    /// duplicated here.
    pub gps_satellites: u8,
    /// Which SD icon `statusRender` would choose.
    pub sd: SdStatus,
}

/// Everything the tasks share, as one value.
///
/// Held by [`SharedState`]; obtained as a copy through
/// [`SharedState::snapshot`].
#[derive(Debug, Clone, PartialEq, Default)]
pub struct AppState {
    /// `map_position`. `None` is the C NULL, which every reader checks for
    /// (`src/screens/map_screen.c:59`, `:84`, `:141`,
    /// `src/esp32/gui_map_callbacks.c:183`).
    pub position: Option<MapPosition>,
    /// `current_battery_level` and `is_charging`.
    pub battery: BatteryState,
    /// The status-bar indicators.
    pub indicators: Indicators,
}

/// The lock around [`AppState`].
///
/// Tasks get an `Arc<SharedState>` at spawn time. There is no `static`, and
/// therefore no `static mut` to audit.
#[derive(Debug, Default)]
pub struct SharedState {
    state: Mutex<AppState>,
}

impl SharedState {
    /// Create the state, empty — the C globals' zero-initialised values
    /// (`map_position` NULL, `current_battery_level` 0, `is_charging` 0,
    /// `wifi_indicator_image_data` `WIFI_0`).
    pub fn new() -> Self {
        SharedState::default()
    }

    /// Take the lock.
    ///
    /// Never poisons, for the reason in [`super::sync`]: a panicking writer
    /// must not make the position permanently unreadable.
    fn lock(&self) -> MutexGuard<'_, AppState> {
        self.state.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Copy the whole state out and drop the lock.
    ///
    /// This is what a render pass should call: it gets a coherent view of
    /// position, battery and indicators, and holds the lock for the length of
    /// a struct copy rather than the length of a display flush.
    pub fn snapshot(&self) -> AppState {
        self.lock().clone()
    }

    /// Read one field under the lock.
    ///
    /// For the cases where a snapshot is more than the caller needs, e.g. the
    /// heap log at `src/esp32/main.c:354`.
    pub fn with<R>(&self, f: impl FnOnce(&AppState) -> R) -> R {
        f(&self.lock())
    }

    /// Mutate the state under the lock.
    ///
    /// The closure must not block: no I/O, no `wait`, no nested lock. See the
    /// module header.
    pub fn update<R>(&self, f: impl FnOnce(&mut AppState) -> R) -> R {
        f(&mut self.lock())
    }

    /// Publish a new position.
    ///
    /// Replaces `map_position = &current_position` (`src/esp32/gps.c:153`).
    pub fn set_position(&self, position: MapPosition) {
        self.update(|state| state.position = Some(position));
    }

    /// The latest position, if there is one.
    pub fn position(&self) -> Option<MapPosition> {
        self.with(|state| state.position)
    }

    /// Publish a battery reading.
    ///
    /// Replaces the `current_battery_level = readBatteryPercent(...)` /
    /// `is_charging = ...` pair (`src/esp32/main.c:132-143`, `:474`), which in
    /// C are two separate unsynchronised stores.
    pub fn set_battery(&self, battery: BatteryState) {
        self.update(|state| state.battery = battery);
    }

    /// The latest battery reading.
    pub fn battery(&self) -> BatteryState {
        self.with(|state| state.battery)
    }

    /// Publish a WiFi signal bucket.
    ///
    /// Replaces `wifi_indicator_image_data = WIFI_n`
    /// (`src/esp32/wifi.c:219-227`).
    pub fn set_wifi_level(&self, level: WifiLevel) {
        self.update(|state| state.indicators.wifi = level);
    }

    /// Publish the SD mount result.
    ///
    /// Replaces the `sd_status` that `statusRender` reads
    /// (`src/esp32/sd.c:63-71`).
    pub fn set_sd_status(&self, status: SdStatus) {
        self.update(|state| state.indicators.sd = status);
    }

    /// Publish the clock text.
    ///
    /// Replaces `sprintf(clock_label->text, ...)` (`src/esp32/gui.c:157`);
    /// build the string with [`super::fmt::guarded_format`].
    pub fn set_clock_text(&self, text: String) {
        self.update(|state| state.indicators.clock = text);
    }

    /// Publish the satellite count shown next to the GPS icon.
    ///
    /// Replaces `save_sprintf(gps_indicator_label->text, "%d", ...)`
    /// (`src/screens/map_screen.c:107`).
    pub fn set_gps_satellites(&self, satellites: u8) {
        self.update(|state| state.indicators.gps_satellites = satellites);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn gps_fix_discriminants_match_gps_fix_t() {
        assert_eq!(GpsFix::Invalid.as_raw(), 0);
        assert_eq!(GpsFix::Gps.as_raw(), 1);
        assert_eq!(GpsFix::Dgps.as_raw(), 2);
        // The C enum jumps: GPS_FIX_DR = 6.
        assert_eq!(GpsFix::DeadReckoning.as_raw(), 6);
        for raw in 0u8..=255 {
            match GpsFix::from_raw(raw) {
                Some(fix) => assert_eq!(fix.as_raw(), raw),
                None => assert!(matches!(raw, 3..=5 | 7..=255)),
            }
        }
        assert!(!GpsFix::Invalid.is_valid());
        assert!(GpsFix::Gps.is_valid());
        assert!(GpsFix::DeadReckoning.is_valid());
    }

    #[test]
    fn wifi_buckets_match_the_c_rssi_thresholds() {
        // src/esp32/wifi.c:216-227
        assert_eq!(WifiLevel::from_rssi(-10), WifiLevel::Strong);
        assert_eq!(WifiLevel::from_rssi(-70), WifiLevel::Strong);
        assert_eq!(WifiLevel::from_rssi(-71), WifiLevel::Medium);
        assert_eq!(WifiLevel::from_rssi(-80), WifiLevel::Medium);
        assert_eq!(WifiLevel::from_rssi(-81), WifiLevel::Weak);
        assert_eq!(WifiLevel::from_rssi(-128), WifiLevel::Weak);
        // The initial value of wifi_indicator_image_data is WIFI_0.
        assert_eq!(WifiLevel::default(), WifiLevel::Disconnected);
    }

    #[test]
    fn a_fresh_state_matches_the_zero_initialised_c_globals() {
        let state = SharedState::new();
        let snapshot = state.snapshot();
        assert_eq!(snapshot.position, None, "map_position is NULL");
        assert_eq!(snapshot.battery, BatteryState::default());
        assert_eq!(snapshot.battery.level, 0);
        assert!(!snapshot.battery.charging);
        assert_eq!(snapshot.indicators.wifi, WifiLevel::Disconnected);
        assert_eq!(snapshot.indicators.sd, SdStatus::Unknown);
        assert_eq!(snapshot.indicators.clock, "");
        assert_eq!(snapshot.indicators.gps_satellites, 0);
    }

    #[test]
    fn publishing_and_reading_round_trips() {
        let state = SharedState::new();
        let position = MapPosition {
            latitude: 48.137_154,
            longitude: 11.576_124,
            altitude: 519.0,
            hdop: 1.2,
            zoom_level: 16,
            fix: GpsFix::Gps,
            satellites_in_view: 11,
            satellites_in_use: 8,
        };
        state.set_position(position);
        state.set_battery(BatteryState {
            level: 73,
            charging: true,
        });
        state.set_wifi_level(WifiLevel::from_rssi(-75));
        state.set_sd_status(SdStatus::Mounted);
        state.set_clock_text("07:05".to_string());
        state.set_gps_satellites(11);

        let snapshot = state.snapshot();
        assert_eq!(snapshot.position, Some(position));
        assert_eq!(snapshot.battery.level, 73);
        assert!(snapshot.battery.charging);
        assert_eq!(snapshot.indicators.wifi, WifiLevel::Medium);
        assert_eq!(snapshot.indicators.sd, SdStatus::Mounted);
        assert_eq!(snapshot.indicators.clock, "07:05");
        assert_eq!(snapshot.indicators.gps_satellites, 11);
        assert_eq!(state.position(), Some(position));
        assert_eq!(state.battery().level, 73);
    }

    /// A snapshot is coherent: it never mixes fields from two writes. This is
    /// the property the C code could not offer.
    #[test]
    fn a_snapshot_never_mixes_two_writes() {
        let state = Arc::new(SharedState::new());
        let writer = {
            let state = state.clone();
            thread::spawn(move || {
                for i in 0..2000u32 {
                    let n = (i % 251) as u8;
                    state.update(|s| {
                        s.position = Some(MapPosition {
                            latitude: n as f32,
                            longitude: n as f32,
                            satellites_in_view: n,
                            satellites_in_use: n,
                            fix: GpsFix::Gps,
                            ..MapPosition::default()
                        });
                        s.battery = BatteryState {
                            level: n as i32,
                            charging: n % 2 == 0,
                        };
                    });
                }
            })
        };
        for _ in 0..2000 {
            let snapshot = state.snapshot();
            if let Some(position) = snapshot.position {
                assert_eq!(position.latitude, position.longitude);
                assert_eq!(position.satellites_in_view, position.satellites_in_use);
                assert_eq!(snapshot.battery.level, position.satellites_in_view as i32);
                assert_eq!(
                    snapshot.battery.charging,
                    position.satellites_in_use % 2 == 0
                );
            }
        }
        writer.join().unwrap();
    }

    /// A panicking writer must not make the state permanently unreadable.
    #[test]
    fn the_state_lock_does_not_poison() {
        let state = Arc::new(SharedState::new());
        state.set_gps_satellites(4);
        let panicker = {
            let state = state.clone();
            thread::spawn(move || {
                state.update(|_| panic!("writer blew up"));
            })
        };
        assert!(panicker.join().is_err());
        assert_eq!(state.snapshot().indicators.gps_satellites, 4);
        state.set_gps_satellites(9);
        assert_eq!(state.snapshot().indicators.gps_satellites, 9);
    }
}
