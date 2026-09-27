//! Fixed-capacity byte buffer.
//!
//! Replaces the `char buf[255]` scratch space of `lib/Platinenmacher/parser/gpx.c`.
//! The firmware parses straight off the SD card, so nothing here allocates: an
//! oversized token sets [`Buf::overflow`] instead of growing.

/// Byte buffer with a compile-time capacity of `N`.
#[derive(Clone, Copy)]
pub(crate) struct Buf<const N: usize> {
    bytes: [u8; N],
    len: usize,
    overflow: bool,
}

impl<const N: usize> Buf<N> {
    pub(crate) const fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
            overflow: false,
        }
    }

    pub(crate) fn clear(&mut self) {
        self.len = 0;
        self.overflow = false;
    }

    /// Appends `b`, or flags an overflow once the capacity is used up.
    pub(crate) fn push(&mut self, b: u8) {
        if self.len < N {
            self.bytes[self.len] = b;
            self.len += 1;
        } else {
            self.overflow = true;
        }
    }

    pub(crate) fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    /// True when the buffer holds exactly `s`. An overflowed buffer never
    /// matches, so an over-long element name is simply an element we skip.
    pub(crate) fn matches(&self, s: &str) -> bool {
        !self.overflow && self.as_bytes() == s.as_bytes()
    }

    /// Contents as UTF-8 with surrounding whitespace removed, or `None` if the
    /// bytes are not valid UTF-8 (a truncated multi-byte character, say).
    pub(crate) fn trimmed_str(&self) -> Option<&str> {
        core::str::from_utf8(self.as_bytes()).ok().map(str::trim)
    }
}
