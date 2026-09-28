//! Port of `lib/Platinenmacher/error.h` (`error_code_t`).
//!
//! **Temporary home.** `rust/PORTING.md` puts this enum in `pm-core` as
//! `pm_core::Error`, and every public API returns `Result<T, Error>`. At the
//! time `pm-parser` was ported the `pm-core` port had not landed on this
//! branch, so the enum is mirrored here to keep the crate compiling and
//! testable standalone. Integration is a one-line change: delete the enum
//! below and replace it with
//!
//! ```text
//! pub use pm_core::Error;
//! ```
//!
//! The variants below mirror `error_code_t` one-for-one, including the
//! misspelled `DELEAYED`, which becomes [`Error::Delayed`]. `PM_OK` has no
//! variant: success is `Ok(())`.

use core::fmt;

/// Failure half of `error_code_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Error {
    /// `PM_FAIL`
    Fail,
    /// `DELEAYED` (sic)
    Delayed,
    /// `OUT_OF_BOUNDS`
    OutOfBounds,
    /// `UNAVAILABLE`
    Unavailable,
    /// `ABORT`
    Abort,
    /// `NOT_NEEDED`
    NotNeeded,
    /// `TIMEOUT`
    Timeout,
    /// `DEFERRED`
    Deferred,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Error::Fail => "failed",
            Error::Delayed => "delayed",
            Error::OutOfBounds => "out of bounds",
            Error::Unavailable => "unavailable",
            Error::Abort => "aborted",
            Error::NotNeeded => "not needed",
            Error::Timeout => "timeout",
            Error::Deferred => "deferred",
        };
        f.write_str(s)
    }
}

impl std::error::Error for Error {}
