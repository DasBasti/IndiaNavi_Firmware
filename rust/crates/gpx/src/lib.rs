//! GPX track reading.
//!
//! Replaces: lib/Platinenmacher/parser/gpx.c, lib/Platinenmacher/parser/gpx.h.
//!
//! Split out of `pm-parser` because it is the only parser that depends on the
//! GUI waypoint model. Pure string processing, host-testable.

pub mod model;
pub mod parse;
