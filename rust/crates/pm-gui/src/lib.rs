//! GUI widgets.
//!
//! Replaces: lib/Platinenmacher/gui/* (except geometric.h, which lives in
//! `pm_core::geometry`).
//!
//! Widgets render into anything implementing `pm_core::display::DisplayTarget`,
//! so the whole crate is testable on the host against an in-memory target.

pub mod battery_indicator;
pub mod graph;
pub mod image;
pub mod label;
pub mod map;
pub mod waypoint;
