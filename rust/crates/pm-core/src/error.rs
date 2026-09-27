//! Error codes.
//!
//! Replaces `lib/Platinenmacher/error.h` (`error_code_t`).

/// Port of `error_code_t`.
///
/// `PM_OK` has no variant: fallible functions return [`Result`], where `Ok`
/// stands for `PM_OK`. The discriminants match the C enum values so that logs
/// and traces stay comparable with the C firmware.
///
/// The C enum spells its second code `DELEAYED`; it is [`Error::Delayed`] here.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(u8)]
pub enum Error {
    /// `PM_FAIL`
    Fail = 1,
    /// `DELEAYED` (sic, in the C header)
    Delayed = 2,
    /// `OUT_OF_BOUNDS`
    OutOfBounds = 3,
    /// `UNAVAILABLE`
    Unavailable = 4,
    /// `ABORT`
    Abort = 5,
    /// `NOT_NEEDED`
    NotNeeded = 6,
    /// `TIMEOUT`
    Timeout = 7,
    /// `DEFERRED`
    Deferred = 8,
}

impl Error {
    /// The value this error has in the C `error_code_t` enum.
    ///
    /// `PM_OK` is 0 and therefore never returned here.
    pub const fn code(self) -> u8 {
        self as u8
    }

    /// The name of the matching C enumerator, original spelling included.
    pub const fn c_name(self) -> &'static str {
        match self {
            Error::Fail => "PM_FAIL",
            Error::Delayed => "DELEAYED",
            Error::OutOfBounds => "OUT_OF_BOUNDS",
            Error::Unavailable => "UNAVAILABLE",
            Error::Abort => "ABORT",
            Error::NotNeeded => "NOT_NEEDED",
            Error::Timeout => "TIMEOUT",
            Error::Deferred => "DEFERRED",
        }
    }
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(self.c_name())
    }
}

/// Result of every fallible Platinenmacher call; `Ok` is `PM_OK`.
pub type Result<T> = core::result::Result<T, Error>;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn codes_match_the_c_enum() {
        assert_eq!(1, Error::Fail.code());
        assert_eq!(2, Error::Delayed.code());
        assert_eq!(3, Error::OutOfBounds.code());
        assert_eq!(4, Error::Unavailable.code());
        assert_eq!(5, Error::Abort.code());
        assert_eq!(6, Error::NotNeeded.code());
        assert_eq!(7, Error::Timeout.code());
        assert_eq!(8, Error::Deferred.code());
    }

    #[test]
    fn keeps_the_original_misspelling_in_the_name() {
        assert_eq!("DELEAYED", Error::Delayed.c_name());
    }
}
