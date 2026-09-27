/*
 * Statement dispatch, ported from parse_item() in nmea_parser.c
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! Statement recognition and per item dispatch.
//!
//! This is the sentence core's dispatch half: it decides which statement an
//! item 0 starts and routes the following items, exactly as `parse_item()` in
//! `nmea_parser.c` does. Extracting the fields of the six built in talker
//! sentences is the sentence core's own job, so [`Dispatcher`] recognises them
//! and hands them no further.

use crate::error::Result;
use crate::plugin::{PluginRegistry, SentenceItem};

/// Base of the plugin statement IDs, mirroring `STATEMENT_PLUGIN`.
///
/// The C code stores a claimed plugin sentence as `STATEMENT_PLUGIN + index` in
/// `esp_gps->cur_statement`.
pub const STATEMENT_PLUGIN: u8 = 7;

/// The statement a sentence belongs to, mirroring `nmea_statement_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Statement {
    /// `STATEMENT_UNKNOWN`: recognised by neither the core nor any plugin.
    Unknown,
    /// `STATEMENT_GGA`
    Gga,
    /// `STATEMENT_GSA`
    Gsa,
    /// `STATEMENT_RMC`
    Rmc,
    /// `STATEMENT_GSV`
    Gsv,
    /// `STATEMENT_GLL`
    Gll,
    /// `STATEMENT_VTG`
    Vtg,
    /// `STATEMENT_PLUGIN + index`: claimed by the plugin at `index`.
    Plugin(u8),
}

impl Statement {
    /// The `nmea_statement_t` value this maps to.
    pub const fn as_raw(self) -> u8 {
        match self {
            Statement::Unknown => 0,
            Statement::Gga => 1,
            Statement::Gsa => 2,
            Statement::Rmc => 3,
            Statement::Gsv => 4,
            Statement::Gll => 5,
            Statement::Vtg => 6,
            Statement::Plugin(index) => STATEMENT_PLUGIN + index,
        }
    }

    /// The statement a `nmea_statement_t` value denotes.
    pub const fn from_raw(raw: u8) -> Self {
        match raw {
            1 => Statement::Gga,
            2 => Statement::Gsa,
            3 => Statement::Rmc,
            4 => Statement::Gsv,
            5 => Statement::Gll,
            6 => Statement::Vtg,
            0 => Statement::Unknown,
            _ => Statement::Plugin(raw - STATEMENT_PLUGIN),
        }
    }
}

/// The statement of an item 0 such as `"$GPGGA"` or `"$PMTK001"`.
///
/// Tries the six built in talker sentences first and only then the plugins,
/// keeping the order of the `if` chain in `parse_item()`. A sentence no plugin
/// claims is [`Statement::Unknown`].
pub fn classify(item: &SentenceItem<'_>, registry: &mut PluginRegistry<'_>) -> Statement {
    if item.text.contains("GGA") {
        Statement::Gga
    } else if item.text.contains("GSA") {
        Statement::Gsa
    } else if item.text.contains("RMC") {
        Statement::Rmc
    } else if item.text.contains("GSV") {
        Statement::Gsv
    } else if item.text.contains("GLL") {
        Statement::Gll
    } else if item.text.contains("VTG") {
        Statement::Vtg
    } else {
        match registry.detect(item) {
            Some(index) => Statement::Plugin(index),
            None => Statement::Unknown,
        }
    }
}

/// Feeds the items of a sentence to the core or to a plugin.
pub struct Dispatcher<'p> {
    registry: PluginRegistry<'p>,
    current: Statement,
}

impl<'p> Dispatcher<'p> {
    /// A dispatcher over `registry`, before any sentence has started.
    pub fn new(registry: PluginRegistry<'p>) -> Self {
        Self {
            registry,
            current: Statement::Unknown,
        }
    }

    /// The statement of the sentence being fed, like `esp_gps->cur_statement`.
    pub fn statement(&self) -> Statement {
        self.current
    }

    /// The plugins this dispatcher routes to.
    pub fn registry_mut(&mut self) -> &mut PluginRegistry<'p> {
        &mut self.registry
    }

    /// Handles one item, in the order `gps_decode()` produces them.
    ///
    /// Item 0 selects the statement and is never parsed further. Items of an
    /// unclaimed sentence are dropped without an error, which is the
    /// `STATEMENT_UNKNOWN` fallthrough of `parse_item()`; the C parser reports
    /// those sentences to the application as a `GPS_UNKNOWN` event instead.
    pub fn feed_item(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        if item.num == 0 && item.text.starts_with('$') {
            self.current = classify(item, &mut self.registry);
            return Ok(());
        }
        match self.current {
            Statement::Unknown => Ok(()),
            Statement::Plugin(index) => self.registry.parse(index, item),
            // Field extraction for the built in statements lives in the
            // sentence core, not here.
            _ => Ok(()),
        }
    }

    /// Feeds a whole sentence, `"$PMTK001,353,3*30\r\n"` style, and returns the
    /// statement it was dispatched to.
    ///
    /// `gps_decode()` discards what `parse_item()` returns, so every item is
    /// fed even after one fails; the first error is returned once the sentence
    /// is through.
    pub fn feed_sentence(&mut self, sentence: &str) -> Result<Statement> {
        let mut first_error = None;
        for item in sentence_items(sentence) {
            if let Err(err) = self.feed_item(&item) {
                first_error.get_or_insert(err);
            }
        }
        match first_error {
            Some(err) => Err(err),
            None => Ok(self.current),
        }
    }
}

/// The items of a sentence, as `gps_decode()` splits them.
///
/// Splits on `,`, keeps the `$` on item 0 and stops at the `*` before the
/// checksum or at the end of the line, because `gps_decode()` never passes the
/// checksum digits to `parse_item()`.
pub fn sentence_items(sentence: &str) -> impl Iterator<Item = SentenceItem<'_>> {
    let payload = sentence.split(['*', '\r', '\n']).next().unwrap_or(sentence);
    payload
        .split(',')
        .enumerate()
        .map(|(num, text)| SentenceItem::new(num as u8, text))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sentence_items_splits_like_gps_decode() {
        let items: Vec<_> = sentence_items("$PMTK001,353,3*35\r\n").collect();
        assert_eq!(
            items,
            [
                SentenceItem::new(0, "$PMTK001"),
                SentenceItem::new(1, "353"),
                SentenceItem::new(2, "3"),
            ],
        );
    }

    #[test]
    fn sentence_items_keeps_empty_fields() {
        let items: Vec<_> = sentence_items("$GPRMC,,A,,*00").collect();
        assert_eq!(items.len(), 5);
        assert_eq!(items[1], SentenceItem::new(1, ""));
        assert_eq!(items[4], SentenceItem::new(4, ""));
    }

    #[test]
    fn sentence_items_handles_a_sentence_without_a_checksum() {
        let items: Vec<_> = sentence_items("$PQGLP,W,OK").collect();
        assert_eq!(items.len(), 3);
        assert_eq!(items[2], SentenceItem::new(2, "OK"));
    }
}
