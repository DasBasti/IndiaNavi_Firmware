//! Text parsers.
//!
//! Replaces: lib/Platinenmacher/parser/{command,config}.{c,h} and lib/sxml/*.
//! The GPX reader that also uses the XML parser lives in the `gpx` crate.
//!
//! Pure string processing, fully host-testable.

pub mod command;
pub mod config;
pub mod xml;
