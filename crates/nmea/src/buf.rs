/*
 * Fixed capacity string buffer, replacing the char arrays of the C parser
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! Fixed capacity string buffers.

use crate::error::{Error, Result};

/// A string of at most `N` bytes, held inline.
///
/// The C parser keeps its strings in fixed `char` arrays such as
/// `esp_gps_t::item_str`; this is the same, without needing an allocator.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FixedStr<const N: usize> {
    bytes: [u8; N],
    len: usize,
}

impl<const N: usize> Default for FixedStr<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> FixedStr<N> {
    /// An empty buffer.
    pub const fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }

    /// Appends `text`, or fails with [`Error::InvalidArg`] and leaves the buffer
    /// untouched when it does not fit.
    pub fn push_str(&mut self, text: &str) -> Result<()> {
        let end = self.len + text.len();
        if end > N {
            return Err(Error::InvalidArg);
        }
        self.bytes[self.len..end].copy_from_slice(text.as_bytes());
        self.len = end;
        Ok(())
    }

    /// Replaces the contents with `text`.
    pub fn set(&mut self, text: &str) -> Result<()> {
        self.clear();
        self.push_str(text)
    }

    /// Empties the buffer.
    pub fn clear(&mut self) {
        self.len = 0;
    }

    /// The buffered text.
    pub fn as_str(&self) -> &str {
        // Only whole `&str` values are ever pushed, so the buffer never splits
        // a UTF-8 sequence.
        core::str::from_utf8(&self.bytes[..self.len]).unwrap_or("")
    }

    /// The buffered bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    /// Number of buffered bytes.
    pub fn len(&self) -> usize {
        self.len
    }

    /// Whether nothing is buffered.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

impl<const N: usize> core::fmt::Write for FixedStr<N> {
    fn write_str(&mut self, text: &str) -> core::fmt::Result {
        self.push_str(text).map_err(|_| core::fmt::Error)
    }
}
