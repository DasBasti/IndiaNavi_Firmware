//! Geometric primitives.
//!
//! Replaces `lib/Platinenmacher/gui/geometric.h`. The `length()` macro is not
//! ported: it is unused in the C tree (only a commented-out line in
//! `gui/waypoint.c`) and would drag `sqrt()` into a `no_std` crate.

/// Port of `point_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Hash)]
pub struct Point {
    pub left: i16,
    pub top: i16,
}

impl Point {
    pub const fn new(left: i16, top: i16) -> Self {
        Self { left, top }
    }
}

/// Port of `rect_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Hash)]
pub struct Rect {
    pub left: i16,
    pub top: i16,
    pub width: u16,
    pub height: u16,
}

impl Rect {
    pub const fn new(left: i16, top: i16, width: u16, height: u16) -> Self {
        Self {
            left,
            top,
            width,
            height,
        }
    }
}

/// Port of `alignment_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Alignment {
    Left = 0,
    Center = 1,
    Right = 2,
    Top = 3,
    Middle = 4,
    Bottom = 5,
}

/// Port of `corner_t`. The C enumerators are bit flags and are combined with
/// `|`, so they stay plain constants instead of becoming an enum.
pub mod corner {
    pub const TOP_LEFT: u8 = 0x1;
    pub const TOP_RIGHT: u8 = 0x2;
    pub const BOTTOM_LEFT: u8 = 0x4;
    pub const BOTTOM_RIGHT: u8 = 0x8;
}

/// Port of `border_line_t`, bit flags like [`corner`].
pub mod border_line {
    pub const NO_BORDER: u8 = 0;
    pub const TOP_SOLID: u8 = 0x1;
    pub const TOP_DOTTED: u8 = 0x2;
    pub const BOTTOM_SOLID: u8 = 0x4;
    /// `BOTTOM_DOTTET` in the C header
    pub const BOTTOM_DOTTED: u8 = 0x8;
    pub const LEFT_SOLID: u8 = 0x10;
    pub const LEFT_DOTTED: u8 = 0x20;
    pub const RIGHT_SOLID: u8 = 0x40;
    /// `RIGHT_DOTTET` in the C header
    pub const RIGHT_DOTTED: u8 = 0x80;
    pub const ALL_SOLID: u8 = 0x55;
    /// `ALL_DOTTET` in the C header
    pub const ALL_DOTTED: u8 = 0xaa;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rect_and_point_keep_the_c_field_order_and_widths() {
        let r = Rect::new(-1, -2, 3, 4);
        assert_eq!((-1, -2, 3, 4), (r.left, r.top, r.width, r.height));
        let p = Point::new(-5, 6);
        assert_eq!((-5, 6), (p.left, p.top));
    }

    #[test]
    fn flag_values_match_the_c_enumerators() {
        assert_eq!(0x8, corner::BOTTOM_RIGHT);
        assert_eq!(
            border_line::ALL_SOLID,
            border_line::TOP_SOLID
                | border_line::BOTTOM_SOLID
                | border_line::LEFT_SOLID
                | border_line::RIGHT_SOLID
        );
        assert_eq!(
            border_line::ALL_DOTTED,
            border_line::TOP_DOTTED
                | border_line::BOTTOM_DOTTED
                | border_line::LEFT_DOTTED
                | border_line::RIGHT_DOTTED
        );
        assert_eq!(0, border_line::NO_BORDER);
        assert_eq!(2, Alignment::Right as u8);
    }
}
