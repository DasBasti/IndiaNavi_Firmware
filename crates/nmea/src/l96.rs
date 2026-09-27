/*
 * Command set of the Quectel L96 GNSS module
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! The L96 commands the GPS task sends, ported from `lib/nmea_parser/l96.h`.
//!
//! Each constant is a complete sentence, checksum and `\r\n` included, ready to
//! hand to the UART the way `nmea_send_command()` does.
//!
//! | `l96.h`                          | here                           |
//! |----------------------------------|--------------------------------|
//! | `L96_SEARCH_GLONASS`             | [`SEARCH_GLONASS`]             |
//! | `L96_SEARCH_GPS`                 | [`SEARCH_GPS`]                 |
//! | `L96_SEARCH_GPS_GLONASS`         | [`SEARCH_GPS_GLONASS`]         |
//! | `L96_SEARCH_GPS_GLONASS_GALILEO` | [`SEARCH_GPS_GLONASS_GALILEO`] |
//! | `L96_ENTER_FULL_ON`              | [`ENTER_FULL_ON`]              |
//! | `L96_ENTER_STANDBY`              | [`ENTER_STANDBY`]              |
//! | `L96_ENTER_ALLWAYS_LOCATE`       | [`ENTER_ALWAYS_LOCATE`]        |
//! | `L96_REPLY_ALLWAYS_LOCATE`       | [`REPLY_ALWAYS_LOCATE`]        |
//! | `L96_ENTER_GLP`                  | [`ENTER_GLP`]                  |
//! | `L96_EXIT_GLP`                   | [`EXIT_GLP`]                   |
//! | `L96_REPLY_GLP`                  | [`REPLY_GLP`]                  |
//! | `L96_AIC_ENABLE`                 | [`AIC_ENABLE`]                 |
//! | `L96_AIC_DISABLE`                | [`AIC_DISABLE`]                |

/// Search GLONASS satellites only.
pub const SEARCH_GLONASS: &str = "$PMTK353,0,1,0,0,0*2A\r\n";
/// Search GPS satellites only.
pub const SEARCH_GPS: &str = "$PMTK353,1,0,0,0,0*2A\r\n";
/// Search GPS and GLONASS satellites.
pub const SEARCH_GPS_GLONASS: &str = "$PMTK353,1,1,0,0,0*2B\r\n";
/// Search GPS, GLONASS and Galileo satellites.
///
/// Sent once at startup by `StartGpsTask()`.
pub const SEARCH_GPS_GLONASS_GALILEO: &str = "$PMTK353,1,1,1,0,0*2A\r\n";

/// Leave a low power mode and run full on: wakes the module from standby.
pub const ENTER_FULL_ON: &str = "$PMTK225,0*2B\r\n";

/// Enter standby. Sent by `gps_enter_standby()`.
pub const ENTER_STANDBY: &str = "$PMTK161,0*28\r\n";

/// Enter AlwaysLocate, in which the module picks its own duty cycle.
///
/// `l96.h` spells this `L96_ENTER_ALLWAYS_LOCATE`.
pub const ENTER_ALWAYS_LOCATE: &str = "$PMTK225,8*23\r\n";
/// The reply to [`ENTER_ALWAYS_LOCATE`].
pub const REPLY_ALWAYS_LOCATE: &str = "$PMTK001,225,3*35\r\n";

/// Enter GLP (GNSS Low Power). Sent once at startup by `StartGpsTask()`.
pub const ENTER_GLP: &str = "$PQGLP,W,1,1*21\r\n";
/// Leave GLP.
pub const EXIT_GLP: &str = "$PQGLP,W,0,1*20\r\n";
/// The reply to [`ENTER_GLP`] and [`EXIT_GLP`].
pub const REPLY_GLP: &str = "$PQGLP,W,OK*09\r\n";

/// Enable AIC, active interference cancellation.
///
/// `l96.h` writes this as `"$PMTK 286,1*23\r\n"`. The space does not belong in
/// a PMTK sentence, and `2A` is not its checksum with the space in: `23` is the
/// checksum of `PMTK286,1`, so the space is a typo and is dropped here.
pub const AIC_ENABLE: &str = "$PMTK286,1*23\r\n";
/// Disable AIC.
///
/// `l96.h` writes this as `"$PMTK 286,0*22\r\n"`; see [`AIC_ENABLE`].
pub const AIC_DISABLE: &str = "$PMTK286,0*22\r\n";

/// Every command in this module, for tests and for iterating over the set.
pub const ALL: [&str; 13] = [
    SEARCH_GLONASS,
    SEARCH_GPS,
    SEARCH_GPS_GLONASS,
    SEARCH_GPS_GLONASS_GALILEO,
    ENTER_FULL_ON,
    ENTER_STANDBY,
    ENTER_ALWAYS_LOCATE,
    REPLY_ALWAYS_LOCATE,
    ENTER_GLP,
    EXIT_GLP,
    REPLY_GLP,
    AIC_ENABLE,
    AIC_DISABLE,
];
