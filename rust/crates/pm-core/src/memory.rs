//! Memory helpers.
//!
//! Replaces `lib/Platinenmacher/memory.h`. `RTOS_Malloc()`/`RTOS_Free()` have
//! no counterpart: allocation goes through `alloc` (the firmware installs the
//! ESP-IDF heap as global allocator), and zeroed memory comes from
//! `alloc::vec![0; n]`, which is what `RTOS_Malloc()` did with its `memset()`.
//!
//! Only the bit macros remain. Unlike the C macros they do not assign to their
//! argument, they return the new value.

/// `bit_set(data, pos)`
pub const fn bit_set(data: u32, pos: u32) -> u32 {
    data | (1 << pos)
}

/// `bit_clear(data, pos)`
pub const fn bit_clear(data: u32, pos: u32) -> u32 {
    data & !(1 << pos)
}

/// `bit_toggle(data, pos)`
pub const fn bit_toggle(data: u32, pos: u32) -> u32 {
    data ^ (1 << pos)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bits_are_set_cleared_and_toggled() {
        assert_eq!(0b0100, bit_set(0, 2));
        assert_eq!(0b0001, bit_clear(0b0101, 2));
        assert_eq!(0b0101, bit_toggle(0b0001, 2));
        assert_eq!(0b0001, bit_toggle(0b0101, 2));
    }
}
