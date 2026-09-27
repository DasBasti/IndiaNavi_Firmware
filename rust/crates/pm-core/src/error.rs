//! Error type shared by every crate in the port.
//!
//! Replaces: lib/Platinenmacher/error.h

/// The failure half of the C `error_code_t`.
///
/// `PM_OK` has no variant here on purpose: success is expressed as
/// `Ok(_)`, so every public API in the port returns `Result<T, Error>`
/// rather than an out-parameter plus a status code.
///
/// The discriminants are pinned to the C values so that a partially ported
/// firmware can still hand an `error_code_t` across the FFI boundary while
/// the C tree exists. Do not reorder; append instead.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
#[non_exhaustive]
pub enum Error {
    /// `PM_FAIL` -- unspecified failure.
    Fail = 1,
    /// `DELEAYED` (sic -- the C spelling is a typo) -- retry later.
    Delayed = 2,
    /// `OUT_OF_BOUNDS` -- coordinate or index outside the valid range.
    OutOfBounds = 3,
    /// `UNAVAILABLE` -- the resource does not exist or is not ready.
    Unavailable = 4,
    /// `ABORT` -- the operation was cancelled.
    Abort = 5,
    /// `NOT_NEEDED` -- nothing to do, not an error for the caller.
    NotNeeded = 6,
    /// `TIMEOUT` -- the operation did not complete in time.
    Timeout = 7,
    /// `DEFERRED` -- handed off to another task; the result comes later.
    Deferred = 8,
}

/// Shorthand for the `Result` every public API in the port returns.
pub type Result<T> = core::result::Result<T, Error>;

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let s = match self {
            Error::Fail => "failed",
            Error::Delayed => "delayed",
            Error::OutOfBounds => "out of bounds",
            Error::Unavailable => "unavailable",
            Error::Abort => "aborted",
            Error::NotNeeded => "not needed",
            Error::Timeout => "timed out",
            Error::Deferred => "deferred",
        };
        f.write_str(s)
    }
}

impl std::error::Error for Error {}

#[cfg(test)]
mod tests {
    use super::*;

    /// The discriminants must match `error_code_t` in
    /// lib/Platinenmacher/error.h, where PM_OK is 0 and the rest follow in
    /// declaration order.
    #[test]
    fn discriminants_match_c_error_code_t() {
        assert_eq!(Error::Fail as u8, 1);
        assert_eq!(Error::Delayed as u8, 2);
        assert_eq!(Error::OutOfBounds as u8, 3);
        assert_eq!(Error::Unavailable as u8, 4);
        assert_eq!(Error::Abort as u8, 5);
        assert_eq!(Error::NotNeeded as u8, 6);
        assert_eq!(Error::Timeout as u8, 7);
        assert_eq!(Error::Deferred as u8, 8);
    }

    /// `PM_OK` is modelled by `Ok`, not by a variant, so no `Error` value may
    /// ever carry the C success code.
    #[test]
    fn no_variant_uses_the_pm_ok_discriminant() {
        for e in [
            Error::Fail,
            Error::Delayed,
            Error::OutOfBounds,
            Error::Unavailable,
            Error::Abort,
            Error::NotNeeded,
            Error::Timeout,
            Error::Deferred,
        ] {
            assert_ne!(e as u8, 0, "{e:?} collides with PM_OK");
        }
    }

    #[test]
    fn error_is_displayable_and_is_a_std_error() {
        let e: &dyn std::error::Error = &Error::Timeout;
        assert_eq!(e.to_string(), "timed out");
    }
}
