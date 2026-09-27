//! QR Code generation.
//!
//! Replaces: lib/qrcodegen/qrcodegen.c, lib/qrcodegen/qrcodegen.h
//! (Nayuki's QR-Code-generator, MIT).
//!
//! Pure computation over byte buffers, so it is fully host-testable.

pub mod bitbuffer;
pub mod encode;
pub mod render;
pub mod segment;
