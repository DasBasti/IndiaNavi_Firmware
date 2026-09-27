//! Typed parse errors.
//!
//! The C parser in `lib/nmea_parser/nmea_parser.c` silently accepted whatever
//! `strtof`/`strtol` returned for a malformed field (`0`), and only logged
//! checksum failures at debug level. Here every rejection is reported to the
//! caller as one of these values, via [`crate::Event::Error`].

use core::fmt;

use crate::parser::SentenceKind;

/// Why a sentence was rejected.
///
/// A rejected sentence never updates [`crate::GpsData`] beyond the fields that
/// were already parsed before the error was hit, and never counts towards the
/// "all required sentences seen" update.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum ParseError {
    /// The checksum after `*` did not match the computed one.
    Checksum {
        /// Checksum computed over the sentence body.
        computed: u8,
        /// Checksum the sentence claimed.
        found: u8,
    },
    /// The sentence ended without a `*` checksum field, or the field was not
    /// exactly two hex digits.
    MalformedChecksum,
    /// A field was longer than [`crate::NMEA_MAX_ITEM_LEN`].
    ItemTooLong {
        /// Zero-based index of the field, `0` being the sentence id.
        index: u8,
    },
    /// The sentence exceeded [`crate::NMEA_MAX_SENTENCE_LEN`] characters.
    SentenceTooLong,
    /// A byte that cannot occur inside an NMEA sentence was received.
    InvalidCharacter {
        /// The offending byte.
        byte: u8,
    },
    /// A numeric field could not be parsed.
    InvalidField {
        /// Sentence the field belongs to.
        statement: SentenceKind,
        /// Zero-based index of the field, `0` being the sentence id.
        index: u8,
    },
    /// A `hhmmss[.sss]` time field was not shaped like one.
    InvalidTime,
    /// A `ddmmyy` date field was not shaped like one.
    InvalidDate,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ParseError::Checksum { computed, found } => {
                write!(
                    f,
                    "checksum mismatch: computed {computed:02X}, found {found:02X}"
                )
            }
            ParseError::MalformedChecksum => write!(f, "missing or malformed checksum field"),
            ParseError::ItemTooLong { index } => write!(f, "field {index} is too long"),
            ParseError::SentenceTooLong => write!(f, "sentence is too long"),
            ParseError::InvalidCharacter { byte } => {
                write!(f, "invalid character {byte:#04x} in sentence")
            }
            ParseError::InvalidField { statement, index } => {
                write!(f, "field {index} of {statement:?} is not a valid number")
            }
            ParseError::InvalidTime => write!(f, "malformed UTC time field"),
            ParseError::InvalidDate => write!(f, "malformed date field"),
        }
    }
}
