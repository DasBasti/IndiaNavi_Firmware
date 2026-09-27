//! NMEA 0183 sentence parser.
//!
//! Replaces the C sources:
//!   * `lib/nmea_parser/nmea_parser.c`
//!   * `lib/nmea_parser/nmea_parser.h`
//!   * `lib/Platinenmacher/gps.{c,h}` (only carried the `gps_fix_t` enum,
//!     ported here as [`GpsFix`]; `gps.c` was empty)
//!
//! Everything UART-, FreeRTOS- and esp-idf-specific from the C parser
//! (the ring buffer, the parser task, the event loop, `nmea_send_command`)
//! deliberately stays out of this crate: it belongs to the firmware crate.
//! What is left is a pure push-bytes state machine — feed it whatever the
//! UART driver read with [`Parser::feed`], get [`Event`]s back.
//!
//! ```
//! use nmea::{Event, Parser};
//!
//! let mut parser = Parser::new();
//! // Only require RMC so a single sentence already produces an update.
//! parser.set_required_sentences(&[nmea::SentenceKind::Rmc]);
//! let events = parser.feed(
//!     b"$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*10\r\n",
//! );
//! assert!(matches!(events.last(), Some(Event::Update(_))));
//! ```
#![no_std]
#![forbid(unsafe_code)]

extern crate alloc;

mod error;
mod gps;
mod parser;

pub use error::ParseError;
pub use gps::{
    GpsData, GpsDate, GpsFix, GpsFixMode, GpsTime, Satellite, GPS_MAX_SATELLITES_IN_USE,
    GPS_MAX_SATELLITES_IN_VIEW,
};
pub use parser::{Event, Parser, SentenceKind, NMEA_MAX_ITEM_LEN, NMEA_MAX_SENTENCE_LEN};
