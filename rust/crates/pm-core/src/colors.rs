//! Available display colors.
//!
//! Replaces `lib/Platinenmacher/colors.h` (`color_t`). The `eink-7color`
//! feature mirrors the `EINK_7COLOR` define of the C header: with it the 7
//! colors of the ACeP 5.65" panel are available, without it only the
//! black/white panel colors.

use crate::error::Error;

/// Available colors on ACeP 5.65", port of `color_t` with `EINK_7COLOR`.
#[cfg(feature = "eink-7color")]
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(u8)]
pub enum Color {
    /// 000
    Black = 0,
    /// 010
    White = 1,
    /// 011
    Green = 2,
    /// 001
    Blue = 3,
    /// 100
    Red = 4,
    /// 101
    Yellow = 5,
    /// 110
    Orange = 6,
    /// 111, unavailable: afterimage. Pixels of this color are not written.
    Transparent = 7,
}

/// Port of `color_t` without `EINK_7COLOR`.
#[cfg(not(feature = "eink-7color"))]
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(u8)]
pub enum Color {
    Black = 0,
    White = 1,
    /// Pixels of this color are not written.
    Transparent = 2,
}

impl Color {
    /// The value this color has in the C `color_t` enum; this is what ends up
    /// in the framebuffer.
    pub const fn as_u8(self) -> u8 {
        self as u8
    }

    /// The color for a raw framebuffer/image byte, or `None` if the value is
    /// not a `color_t` enumerator.
    ///
    /// The C code passes colors around as plain `uint8_t` (see the
    /// `uint8_t color` parameters of `display_rect_draw()` and friends); this
    /// is where that conversion happens explicitly.
    #[cfg(feature = "eink-7color")]
    pub const fn from_u8(value: u8) -> Option<Self> {
        match value {
            0 => Some(Color::Black),
            1 => Some(Color::White),
            2 => Some(Color::Green),
            3 => Some(Color::Blue),
            4 => Some(Color::Red),
            5 => Some(Color::Yellow),
            6 => Some(Color::Orange),
            7 => Some(Color::Transparent),
            _ => None,
        }
    }

    /// The color for a raw framebuffer/image byte, or `None` if the value is
    /// not a `color_t` enumerator.
    #[cfg(not(feature = "eink-7color"))]
    pub const fn from_u8(value: u8) -> Option<Self> {
        match value {
            0 => Some(Color::Black),
            1 => Some(Color::White),
            2 => Some(Color::Transparent),
            _ => None,
        }
    }
}

impl From<Color> for u8 {
    fn from(color: Color) -> u8 {
        color.as_u8()
    }
}

impl TryFrom<u8> for Color {
    type Error = Error;

    /// Fails with [`Error::OutOfBounds`] for values outside `color_t`.
    fn try_from(value: u8) -> Result<Self, Error> {
        Color::from_u8(value).ok_or(Error::OutOfBounds)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn black_is_zero_and_white_is_one_like_in_c() {
        assert_eq!(0, Color::Black.as_u8());
        assert_eq!(1, Color::White.as_u8());
    }

    #[cfg(feature = "eink-7color")]
    #[test]
    fn seven_color_panel_has_the_acep_colors() {
        assert_eq!(2, Color::Green.as_u8());
        assert_eq!(3, Color::Blue.as_u8());
        assert_eq!(4, Color::Red.as_u8());
        assert_eq!(5, Color::Yellow.as_u8());
        assert_eq!(6, Color::Orange.as_u8());
        assert_eq!(7, Color::Transparent.as_u8());
        assert_eq!(Some(Color::Orange), Color::from_u8(6));
        assert_eq!(None, Color::from_u8(8));
    }

    #[cfg(not(feature = "eink-7color"))]
    #[test]
    fn three_color_panel_stops_after_transparent() {
        assert_eq!(2, Color::Transparent.as_u8());
        assert_eq!(None, Color::from_u8(3));
    }

    #[test]
    fn raw_values_convert_both_ways() {
        assert_eq!(Ok(Color::White), Color::try_from(1));
        assert_eq!(Err(Error::OutOfBounds), Color::try_from(200));
        assert_eq!(1u8, u8::from(Color::White));
    }
}
