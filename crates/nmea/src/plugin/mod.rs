/*
 * Plugin layer over the NMEA sentence core
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! Vendor sentence plugins.
//!
//! `nmea_parser.c` dispatches any sentence it does not recognise itself to up
//! to `GPS_MAX_PARSER_PLUGINS` extra parsers, held as an array of
//! `struct nmea_parser_plugin` function pointer pairs in
//! `nmea_parser_config_t`. This module replaces that array with the
//! [`SentencePlugin`] trait plus a [`PluginRegistry`], keeping the dispatch
//! order and the fallthrough behaviour for unclaimed sentences.

pub mod pmtk;
pub mod pq;

use crate::error::{Error, Result};

/// Number of plugin slots, mirroring `GPS_MAX_PARSER_PLUGINS`.
pub const GPS_MAX_PARSER_PLUGINS: usize = 2;

/// One comma separated field of a sentence.
///
/// The C plugins read the field out of `esp_gps_t::item_str` together with its
/// index in `esp_gps_t::item_num`. Item 0 is the sentence identifier and still
/// carries the leading `$`, exactly as `gps_decode()` stores it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SentenceItem<'a> {
    /// Field index within the sentence; 0 is the identifier.
    pub num: u8,
    /// Field text, without the separators around it.
    pub text: &'a str,
}

impl<'a> SentenceItem<'a> {
    /// Builds an item from its index and text.
    pub const fn new(num: u8, text: &'a str) -> Self {
        Self { num, text }
    }

    /// First byte of the field, or 0 when it is empty.
    ///
    /// The C parsers test `esp_gps->item_str[0]`, which reads the terminating
    /// NUL for an empty field.
    pub fn first_byte(&self) -> u8 {
        self.text.as_bytes().first().copied().unwrap_or(0)
    }

    /// The field read as an integer with `atoi()` semantics.
    pub fn as_int(&self) -> i32 {
        atoi(self.text)
    }
}

/// `atoi()`: optional sign, then decimal digits, stopping at the first byte
/// that is not a digit. Anything unparseable, the empty string included, is 0.
pub(crate) fn atoi(text: &str) -> i32 {
    let bytes = text.as_bytes();
    let mut i = 0;
    let negative = match bytes.first() {
        Some(b'-') => {
            i = 1;
            true
        }
        Some(b'+') => {
            i = 1;
            false
        }
        _ => false,
    };
    let mut value: i32 = 0;
    while let Some(digit) = bytes.get(i).and_then(|b| (*b as char).to_digit(10)) {
        value = value.saturating_mul(10).saturating_add(digit as i32);
        i += 1;
    }
    if negative {
        -value
    } else {
        value
    }
}

/// A parser for sentences the core does not handle itself.
///
/// Replaces the two function pointers of `struct nmea_parser_plugin`. Both
/// methods take `&mut self` because the C plugins keep their per sentence state
/// in file scope statics (`messageNumber` in `pmtk_parser.c`, `statement` in
/// `pq_parser.c`); here that state lives in the plugin itself.
pub trait SentencePlugin {
    /// Offered item 0 of every sentence the core did not recognise.
    ///
    /// Returning `Ok(())` claims the sentence, so that every following item is
    /// routed to [`parse`](SentencePlugin::parse). Returning
    /// `Err(Error::NotSupported)` declines it, as `ESP_ERR_NOT_SUPPORTED` does
    /// in C.
    fn detect(&mut self, item: &SentenceItem<'_>) -> Result<()>;

    /// Called for every item after the identifier of a claimed sentence.
    fn parse(&mut self, item: &SentenceItem<'_>) -> Result<()>;
}

/// The plugins a parser instance dispatches to, in registration order.
///
/// Replaces `nmea_parser_config_t::plugins`. `src/esp32/gps.c` fills that array
/// with PMTK first and PQ second; registering in that order reproduces the C
/// dispatch order.
pub struct PluginRegistry<'p> {
    slots: [Option<&'p mut dyn SentencePlugin>; GPS_MAX_PARSER_PLUGINS],
    len: usize,
}

impl<'p> Default for PluginRegistry<'p> {
    fn default() -> Self {
        Self::new()
    }
}

impl<'p> PluginRegistry<'p> {
    /// An empty registry. With no plugins registered every unrecognised
    /// sentence stays unclaimed, like a `NULL` `esp_gps->plugins`.
    pub fn new() -> Self {
        Self {
            slots: core::array::from_fn(|_| None),
            len: 0,
        }
    }

    /// Appends a plugin, or fails with [`Error::RegistryFull`] once
    /// [`GPS_MAX_PARSER_PLUGINS`] plugins are registered.
    pub fn register(&mut self, plugin: &'p mut dyn SentencePlugin) -> Result<()> {
        if self.len == GPS_MAX_PARSER_PLUGINS {
            return Err(Error::RegistryFull);
        }
        self.slots[self.len] = Some(plugin);
        self.len += 1;
        Ok(())
    }

    /// Number of registered plugins.
    pub fn len(&self) -> usize {
        self.len
    }

    /// Whether no plugin is registered.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Offers item 0 to every plugin in registration order and returns the
    /// index of the first one that claims it, or `None` when none does.
    ///
    /// The loop in `parse_item()` has no `break`, so in C the *last* plugin
    /// whose `detect()` succeeds wins. `pmtk_detect()` and `pq_detect()` are
    /// mutually exclusive, so first match is the same dispatch for every
    /// sentence either of them accepts, and it is what the code intends.
    pub fn detect(&mut self, item: &SentenceItem<'_>) -> Option<u8> {
        for (index, slot) in self.slots.iter_mut().enumerate() {
            if let Some(plugin) = slot {
                if plugin.detect(item).is_ok() {
                    return Some(index as u8);
                }
            }
        }
        None
    }

    /// Routes an item to the plugin at `index`.
    ///
    /// Fails with [`Error::NotSupported`] for an index with no plugin, where
    /// the C code would call through a `NULL` pointer.
    pub fn parse(&mut self, index: u8, item: &SentenceItem<'_>) -> Result<()> {
        match self.slots.get_mut(index as usize).and_then(|s| s.as_mut()) {
            Some(plugin) => plugin.parse(item),
            None => Err(Error::NotSupported),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn atoi_stops_at_the_first_byte_that_is_not_a_digit() {
        assert_eq!(atoi("353"), 353);
        assert_eq!(atoi("001"), 1);
        assert_eq!(atoi("15*00"), 15);
        assert_eq!(atoi("-7,"), -7);
        assert_eq!(atoi("+7"), 7);
        assert_eq!(atoi(""), 0);
        assert_eq!(atoi("W"), 0);
        assert_eq!(atoi("-"), 0);
    }

    #[test]
    fn atoi_saturates_instead_of_overflowing() {
        assert_eq!(atoi("99999999999999999999"), i32::MAX);
    }

    #[test]
    fn an_empty_item_reads_as_a_nul_byte() {
        assert_eq!(SentenceItem::new(1, "").first_byte(), 0);
        assert_eq!(SentenceItem::new(1, "W").first_byte(), b'W');
    }
}
