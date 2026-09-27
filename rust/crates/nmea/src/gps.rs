//! GPS data model.
//!
//! Port of `gps_t` and friends from `lib/nmea_parser/nmea_parser.h`, plus the
//! `gps_fix_t` enum that `lib/Platinenmacher/gps.h` duplicated.
//!
//! Field units and scaling are kept identical to the C struct so the firmware
//! and the GPX writer can be ported without touching any arithmetic.

/// Maximum number of satellite IDs recorded from GSA. (`GPS_MAX_SATELLITES_IN_USE`)
pub const GPS_MAX_SATELLITES_IN_USE: usize = 12;
/// Maximum number of satellites described by GSV. (`GPS_MAX_SATELLITES_IN_VIEW`)
pub const GPS_MAX_SATELLITES_IN_VIEW: usize = 16;

/// GPS fix type. Port of `gps_fix_t`.
///
/// The C code stored the raw digit from the GGA fix field, so values the enum
/// did not name (RTK fixes 4/5, manual 7, simulated 8) survived. [`GpsFix::Other`]
/// keeps that information instead of silently collapsing it to `Invalid`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GpsFix {
    /// Not fixed. (`GPS_FIX_INVALID`)
    #[default]
    Invalid,
    /// GPS. (`GPS_FIX_GPS`)
    Gps,
    /// Differential GPS. (`GPS_FIX_DGPS`)
    Dgps,
    /// Dead reckoning, valid fix. (`GPS_FIX_DR`)
    DeadReckoning,
    /// Any other fix indicator reported by the receiver.
    Other(u8),
}

impl GpsFix {
    /// Map the raw GGA fix indicator onto the enum.
    pub fn from_u8(value: u8) -> Self {
        match value {
            0 => GpsFix::Invalid,
            1 => GpsFix::Gps,
            2 => GpsFix::Dgps,
            6 => GpsFix::DeadReckoning,
            other => GpsFix::Other(other),
        }
    }

    /// The raw GGA fix indicator, as the C code stored it.
    pub fn as_u8(self) -> u8 {
        match self {
            GpsFix::Invalid => 0,
            GpsFix::Gps => 1,
            GpsFix::Dgps => 2,
            GpsFix::DeadReckoning => 6,
            GpsFix::Other(other) => other,
        }
    }

    /// Whether this fix indicator means the receiver has a position.
    pub fn is_valid(self) -> bool {
        !matches!(self, GpsFix::Invalid)
    }
}

/// GPS fix mode. Port of `gps_fix_mode_t`.
///
/// Note: the C struct was `calloc`ed, so its initial `fix_mode` was `0`, which
/// is not a value `gps_fix_mode_t` names. Here the default is
/// [`GpsFixMode::Invalid`] (the C `GPS_MODE_INVALID`, raw value 1); both mean
/// "no fix mode reported yet".
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GpsFixMode {
    /// Not fixed. (`GPS_MODE_INVALID`, raw value 1)
    #[default]
    Invalid,
    /// 2D fix. (`GPS_MODE_2D`)
    Fix2D,
    /// 3D fix. (`GPS_MODE_3D`)
    Fix3D,
}

impl GpsFixMode {
    /// Map the raw GSA mode digit onto the enum. Unknown digits are `Invalid`.
    pub fn from_u8(value: u8) -> Self {
        match value {
            2 => GpsFixMode::Fix2D,
            3 => GpsFixMode::Fix3D,
            _ => GpsFixMode::Invalid,
        }
    }

    /// The raw GSA mode digit, as the C code stored it.
    pub fn as_u8(self) -> u8 {
        match self {
            GpsFixMode::Invalid => 1,
            GpsFixMode::Fix2D => 2,
            GpsFixMode::Fix3D => 3,
        }
    }
}

/// Description of one satellite in view. Port of `gps_satellite_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Satellite {
    /// Satellite number.
    pub num: u8,
    /// Satellite elevation, degrees.
    pub elevation: u8,
    /// Satellite azimuth, degrees.
    pub azimuth: u16,
    /// Satellite signal to noise ratio, dBHz.
    pub snr: u8,
}

/// UTC time of the fix. Port of `gps_time_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct GpsTime {
    /// Hour.
    pub hour: u8,
    /// Minute.
    pub minute: u8,
    /// Second.
    pub second: u8,
    /// Fractional seconds, as the digits the receiver sent (`.487` -> `487`).
    pub thousand: u16,
}

/// Date of the fix. Port of `gps_date_t`.
///
/// `year` keeps the C semantics: the two digits from the RMC date field, i.e.
/// years since 2000 (`120598` -> `year == 98`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct GpsDate {
    /// Day of month, starting at 1.
    pub day: u8,
    /// Month, starting at 1.
    pub month: u8,
    /// Two-digit year as sent by the receiver.
    pub year: u16,
}

/// Everything the parser knows about the receiver. Port of `gps_t`.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct GpsData {
    /// Latitude, degrees, positive north.
    pub latitude: f32,
    /// Longitude, degrees, positive east.
    pub longitude: f32,
    /// Altitude, meters (GGA antenna altitude plus geoid separation, as in C).
    pub altitude: f32,
    /// Fix status.
    pub fix: GpsFix,
    /// Number of satellites in use.
    pub sats_in_use: u8,
    /// Time in UTC.
    pub tim: GpsTime,
    /// Fix mode.
    pub fix_mode: GpsFixMode,
    /// IDs of the satellites in use.
    pub sats_id_in_use: [u8; GPS_MAX_SATELLITES_IN_USE],
    /// Horizontal dilution of precision.
    pub dop_h: f32,
    /// Position dilution of precision.
    pub dop_p: f32,
    /// Vertical dilution of precision.
    pub dop_v: f32,
    /// Number of satellites in view.
    pub sats_in_view: u8,
    /// Description of the satellites in view.
    pub sats_desc_in_view: [Satellite; GPS_MAX_SATELLITES_IN_VIEW],
    /// Date of the fix.
    pub date: GpsDate,
    /// GPS validity, from the RMC/GLL status field.
    pub valid: bool,
    /// Ground speed.
    ///
    /// Scaling is carried over from the C parser verbatim: knots are multiplied
    /// by `1.852` (RMC, VTG field 5) and km/h are divided by `3.6` (VTG field 7).
    /// The C code labelled both "m/s", which is only true for the km/h branch —
    /// the knots branch yields km/h. Callers that were written against the C
    /// parser therefore keep working unchanged.
    pub speed: f32,
    /// Course over ground, degrees true.
    pub cog: f32,
    /// Magnetic variation, degrees.
    pub variation: f32,
}
