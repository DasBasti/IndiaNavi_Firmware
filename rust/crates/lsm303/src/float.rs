//! `f32` transcendentals for [`crate::heading`].
//!
//! `core` has no `sin`/`cos`/`asin`/`atan2`, so they come either from `std`
//! (the default, and what the ESP-IDF port links against) or from `libm` for a
//! no_std build. Everything else in this crate is integer-only.

#[cfg(all(feature = "std", feature = "libm"))]
compile_error!("enable exactly one of the `std` and `libm` features, not both");

#[cfg(not(any(feature = "std", feature = "libm")))]
compile_error!("enable one of the `std` or `libm` features to get f32 math");

#[cfg(feature = "std")]
mod imp {
    pub fn sqrt(v: f32) -> f32 {
        v.sqrt()
    }
    pub fn sin(v: f32) -> f32 {
        v.sin()
    }
    pub fn cos(v: f32) -> f32 {
        v.cos()
    }
    pub fn asin(v: f32) -> f32 {
        v.asin()
    }
    pub fn atan2(y: f32, x: f32) -> f32 {
        y.atan2(x)
    }
    pub fn abs(v: f32) -> f32 {
        v.abs()
    }
}

#[cfg(all(feature = "libm", not(feature = "std")))]
mod imp {
    pub fn sqrt(v: f32) -> f32 {
        libm::sqrtf(v)
    }
    pub fn sin(v: f32) -> f32 {
        libm::sinf(v)
    }
    pub fn cos(v: f32) -> f32 {
        libm::cosf(v)
    }
    pub fn asin(v: f32) -> f32 {
        libm::asinf(v)
    }
    pub fn atan2(y: f32, x: f32) -> f32 {
        libm::atan2f(y, x)
    }
    pub fn abs(v: f32) -> f32 {
        libm::fabsf(v)
    }
}

pub(crate) use imp::{abs, asin, atan2, cos, sin, sqrt};
