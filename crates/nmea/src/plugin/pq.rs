/*
 * Parser for Quectel GNSS SDK command responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! PQ, the Quectel GNSS SDK command set.
//!
//! Ported from `lib/nmea_parser/pq_parser.c`. Only `$PQGLP` carries fields the
//! C code looks at; the other statements are recognised and their fields
//! skipped, which is what the empty `case` arms of `pq_parse()` do.

use crate::buf::FixedStr;
use crate::error::{Error, Result};
use crate::plugin::{SentenceItem, SentencePlugin};

/// Longest `$PQGLP` value [`PqPlugin`] keeps.
pub const PQ_VALUE_MAX: usize = 32;

/// The PQ statements `pq_detect()` recognises, in the order it tests for them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PqStatement {
    /// Change NMEA port default baud rate.
    PqBaud,
    /// Enable/Disable PQEPE sentence output.
    PqEpe,
    /// Set the type and pulse width of 1PPS output.
    Pq1Pps,
    /// Set the module into FLP (Fitness Low Power) mode.
    PqFlp,
    /// Enable/Disable GPTXT sentence output.
    PqTxt,
    /// Enable/Disable ECEFPOSVEL sentence output.
    PqEcef,
    /// Start/Stop odometer reading.
    PqOdo,
    /// Enable/Disable switching from WGS84 to PZ-90.11.
    PqPz90,
    /// Set the module into GLP (GNSS Low Power) mode.
    PqGlp,
    /// Enable/Disable 3 ways velocity sentence.
    PqVel,
    /// Enable/Disable jamming detection function.
    PqJam,
    /// Enable/Disable return link message output.
    PqRlm,
    /// Configure parameters of geo-fence.
    PqGeo,
}

impl PqStatement {
    /// The substring `pq_detect()` looks for in the sentence identifier.
    ///
    /// `pq_parser.c` matches `"PQLFP"` for [`PqStatement::PqFlp`], so a real
    /// `$PQFLP` sentence is not detected. The typo is kept here: fixing it
    /// would make the parser claim sentences the C build leaves unclaimed.
    pub const fn marker(self) -> &'static str {
        match self {
            PqStatement::PqBaud => "PQBAUD",
            PqStatement::PqEpe => "PQEPE",
            PqStatement::Pq1Pps => "PQ1PPS",
            PqStatement::PqFlp => "PQLFP",
            PqStatement::PqTxt => "PQTXT",
            PqStatement::PqEcef => "PQECEF",
            PqStatement::PqOdo => "PQODO",
            PqStatement::PqPz90 => "PQPZ90",
            PqStatement::PqGlp => "PQGLP",
            PqStatement::PqVel => "PQVEL",
            PqStatement::PqJam => "PQJAM",
            PqStatement::PqRlm => "PQRLM",
            PqStatement::PqGeo => "PQGEO",
        }
    }

    /// The statements in the order `pq_detect()` tests for them.
    pub const ALL: [PqStatement; 13] = [
        PqStatement::PqBaud,
        PqStatement::PqEpe,
        PqStatement::Pq1Pps,
        PqStatement::PqFlp,
        PqStatement::PqTxt,
        PqStatement::PqEcef,
        PqStatement::PqOdo,
        PqStatement::PqPz90,
        PqStatement::PqGlp,
        PqStatement::PqVel,
        PqStatement::PqJam,
        PqStatement::PqRlm,
        PqStatement::PqGeo,
    ];
}

/// Whether a `$PQGLP` sentence is setting a value or reading one back.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GlpAccess {
    /// Field 1 was `W`: a write.
    Write,
    /// Field 1 was anything else, `R` in practice: a read.
    Read,
}

/// The value of the most recent `$PQGLP` sentence.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GlpValue {
    /// Whether the sentence was a write or a read.
    pub access: GlpAccess,
    value: FixedStr<PQ_VALUE_MAX>,
}

impl GlpValue {
    /// Field 2, the value that was set or read.
    pub fn value(&self) -> &str {
        self.value.as_str()
    }
}

/// Parses the PQ sentences the L96 sends back.
///
/// Replaces `pq_detect()` and `pq_parse()`, and the `statement` and `write`
/// statics they keep between calls.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct PqPlugin {
    /// `statement`, `STATEMENT_UNKNOWN` being `None`.
    statement: Option<PqStatement>,
    /// `write` of `pq_parse_glp()`, which the C code never clears.
    write: bool,
    glp: Option<GlpValue>,
}

impl PqPlugin {
    /// A plugin that has seen nothing yet.
    pub const fn new() -> Self {
        Self {
            statement: None,
            write: false,
            glp: None,
        }
    }

    /// The statement being parsed, or `None` before any was detected.
    pub fn statement(&self) -> Option<PqStatement> {
        self.statement
    }

    /// The value of the most recent `$PQGLP` sentence.
    pub fn glp(&self) -> Option<&GlpValue> {
        self.glp.as_ref()
    }

    /// `pq_parse_glp()`.
    fn parse_glp(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        match item.num {
            1 => {
                if item.text.as_bytes().first() == Some(&b'W') {
                    self.write = true;
                }
            }
            2 => {
                let access = if self.write {
                    GlpAccess::Write
                } else {
                    GlpAccess::Read
                };
                let mut value = FixedStr::new();
                let mut end = item.text.len().min(PQ_VALUE_MAX);
                while end > 0 && !item.text.is_char_boundary(end) {
                    end -= 1;
                }
                value.push_str(&item.text[..end])?;
                self.glp = Some(GlpValue { access, value });
            }
            _ => {}
        }
        Ok(())
    }
}

impl SentencePlugin for PqPlugin {
    /// `pq_detect()`: claims a sentence whose identifier contains one of the
    /// [`PqStatement`] markers.
    fn detect(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        for statement in PqStatement::ALL {
            if item.text.contains(statement.marker()) {
                self.statement = Some(statement);
                return Ok(());
            }
        }
        Err(Error::NotSupported)
    }

    /// `pq_parse()`.
    fn parse(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        match self.statement {
            Some(PqStatement::PqGlp) => self.parse_glp(item),
            // Recognised, but none of its fields are looked at.
            Some(_) => Ok(()),
            // `pq_parse()` without a preceding successful `pq_detect()`.
            None => Err(Error::NotSupported),
        }
    }
}
