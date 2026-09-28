//! Error codes, ported from `lib/Platinenmacher/error.h`.

/// Port of `error_code_t`, minus `PM_OK`, which is expressed as `Ok(())`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Error {
    /// `PM_FAIL`
    Fail,
    /// `DELEAYED` (sic, as spelled in the C header)
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

/// Result of an operation that returns `error_code_t`; `Ok(())` is `PM_OK`.
pub type Result<T = ()> = core::result::Result<T, Error>;
