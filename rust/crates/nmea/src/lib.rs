//! NMEA 0183 parsing.
//!
//! Replaces: lib/nmea_parser/*.
//!
//! The C version owned a UART driver and a FreeRTOS task. Here the crate is a
//! pure `&[u8]` -> sentence decoder; the firmware crate feeds it from a UART
//! and runs the task. That keeps every sentence case host-testable.

pub mod l96;
pub mod parser;
pub mod pmtk;
pub mod pq;
