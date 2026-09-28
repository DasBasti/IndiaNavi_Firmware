//! Glyph placement tests for the port of `display_text_draw()`.
//!
//! Expectations are written as ASCII art rendered by
//! [`MockDisplay::to_ascii`] and were derived by hand from the C font tables,
//! so they pin the pixels rather than restating the implementation.

use pm_core::MockDisplay;
use pm_font::fonts::{FONT6X8, FONT8X8};
use pm_font::{draw_text, draw_text_len, Error, Rotation};

const ON: u8 = 1;

#[test]
fn the_two_fonts_under_test_cover_both_rotations() {
    assert_eq!(FONT6X8.rotation(), Rotation::Rotate0);
    assert_eq!(FONT8X8.rotation(), Rotation::Rotate90);
}

#[test]
fn font8x8_glyph_is_drawn_row_major_at_an_offset() {
    // font8x8[] 'A' == 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00.
    // Rotation 90: byte n is row n, bit i (LSB first) is column i.
    let mut dsp = MockDisplay::new(10, 10);
    assert_eq!(draw_text(&mut dsp, &FONT8X8, 1, 1, b"A", ON), Ok(()));

    assert_eq!(
        dsp.to_ascii(),
        concat!(
            "..........\n",
            "...##.....\n",
            "..####....\n",
            ".##..##...\n",
            ".##..##...\n",
            ".######...\n",
            ".##..##...\n",
            ".##..##...\n",
            "..........\n",
            ".........."
        )
    );
}

#[test]
fn font6x8_glyph_is_drawn_column_major() {
    // font6x8[] 'A' == 0x3E, 0x11, 0x11, 0x11, 0x3E, 0x00.
    // Rotation 0: byte n is column n, bit i (LSB first) is row i.
    let mut dsp = MockDisplay::new(8, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"A", ON), Ok(()));

    assert_eq!(
        dsp.to_ascii(),
        concat!(
            ".###....\n",
            "#...#...\n",
            "#...#...\n",
            "#...#...\n",
            "#####...\n",
            "#...#...\n",
            "........\n",
            "........"
        )
    );
}

#[test]
fn columns_advance_by_eight_pixels_not_by_font_width() {
    // C quirk: `x + (column * 8)` ignores `font->width`, so the 6 pixel wide
    // font leaves a two pixel gap between characters.
    let mut dsp = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"AB", ON), Ok(()));

    assert_eq!(
        dsp.to_ascii(),
        concat!(
            ".###.....###....\n",
            "#...#...#...#...\n",
            "#...#...####....\n",
            "#...#...#...#...\n",
            "#####...#...#...\n",
            "#...#...####....\n",
            "................\n",
            "................"
        )
    );
}

#[test]
fn newline_advances_by_font_height_plus_two() {
    let mut dsp = MockDisplay::new(6, 20);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"A\nA", ON), Ok(()));

    // Second line starts at y = 1 * (8 + 2).
    assert_eq!(dsp.pixel(1, 0), Some(ON));
    assert_eq!(dsp.pixel(1, 10), Some(ON));
    for y in 6..10 {
        for x in 0..6 {
            assert_eq!(dsp.pixel(x, y), Some(0), "gap pixel ({x},{y})");
        }
    }
}

#[test]
fn carriage_return_restarts_the_column() {
    // "A\rA" redraws the same glyph in the same place, so the framebuffer is
    // indistinguishable from a single "A".
    let mut returned = MockDisplay::new(16, 8);
    assert_eq!(
        draw_text(&mut returned, &FONT6X8, 0, 0, b"A\rA", ON),
        Ok(())
    );

    let mut single = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut single, &FONT6X8, 0, 0, b"A", ON), Ok(()));

    assert_eq!(returned.to_ascii(), single.to_ascii());

    // Without the carriage return the second glyph lands at column 1.
    let mut plain = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut plain, &FONT6X8, 0, 0, b"AA", ON), Ok(()));
    assert_eq!(plain.pixel(9, 0), Some(ON));
    assert_eq!(returned.pixel(9, 0), Some(0));
}

#[test]
fn tab_jumps_to_the_next_tab_stop() {
    // C quirk: `column += 8 - ((column + 1) % 8)`, so after one character the
    // cursor lands on column 7, not column 8.
    let mut dsp = MockDisplay::new(80, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"A\tA", ON), Ok(()));

    assert_eq!(dsp.pixel(1, 0), Some(ON)); // column 0
    assert_eq!(dsp.pixel(57, 0), Some(ON)); // column 7 -> x = 56
    for x in 8..56 {
        for y in 0..8 {
            assert_eq!(dsp.pixel(x, y), Some(0), "gap pixel ({x},{y})");
        }
    }
}

#[test]
fn text_leaving_the_display_reports_out_of_bounds() {
    // The glyph spans x = 4..=9 but the display is only 8 wide.
    let mut dsp = MockDisplay::new(8, 8);
    assert_eq!(
        draw_text(&mut dsp, &FONT6X8, 4, 0, b"A", ON),
        Err(Error::OutOfBounds)
    );

    // Everything that did fit is still drawn.
    assert_eq!(
        dsp.to_ascii(),
        concat!(
            ".....###\n",
            "....#...\n",
            "....#...\n",
            "....#...\n",
            "....####\n",
            "....#...\n",
            "........\n",
            "........"
        )
    );
}

#[test]
fn a_fully_visible_glyph_reports_success() {
    let mut dsp = MockDisplay::new(6, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"A", ON), Ok(()));
}

#[test]
fn characters_outside_the_font_are_skipped_but_still_advance() {
    // The C code would index past the end of the table here.
    let mut dsp = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"\x1fA", ON), Ok(()));

    for x in 0..8 {
        for y in 0..8 {
            assert_eq!(dsp.pixel(x, y), Some(0), "pixel ({x},{y})");
        }
    }
    assert_eq!(dsp.pixel(9, 0), Some(ON));
}

#[test]
fn drawing_stops_at_the_nul_terminator() {
    let mut terminated = MockDisplay::new(16, 8);
    assert_eq!(
        draw_text(&mut terminated, &FONT6X8, 0, 0, b"A\0B", ON),
        Ok(())
    );

    let mut single = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut single, &FONT6X8, 0, 0, b"A", ON), Ok(()));

    assert_eq!(terminated.to_ascii(), single.to_ascii());
}

#[test]
fn draw_text_len_honours_its_length() {
    let mut limited = MockDisplay::new(16, 8);
    assert_eq!(
        draw_text_len(&mut limited, &FONT6X8, 0, 0, b"AB", 1, ON),
        Ok(())
    );

    let mut single = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut single, &FONT6X8, 0, 0, b"A", ON), Ok(()));

    assert_eq!(limited.to_ascii(), single.to_ascii());

    // A length past the end of the slice draws the whole slice, it does not panic.
    let mut clamped = MockDisplay::new(16, 8);
    assert_eq!(
        draw_text_len(&mut clamped, &FONT6X8, 0, 0, b"A", 99, ON),
        Ok(())
    );
    assert_eq!(clamped.to_ascii(), single.to_ascii());

    // Unlike draw_text, draw_text_len does not stop at a NUL byte; it renders
    // the font's glyph for every one of the `len` bytes it is given.
    let mut with_nul = MockDisplay::new(24, 8);
    assert_eq!(
        draw_text_len(&mut with_nul, &FONT6X8, 0, 0, b"A\0B", 3, ON),
        Ok(())
    );
    assert_eq!(with_nul.pixel(17, 0), Some(ON)); // 'B' at column 2
}

#[test]
fn an_empty_string_draws_nothing() {
    let mut dsp = MockDisplay::new(8, 8);
    assert_eq!(draw_text(&mut dsp, &FONT6X8, 0, 0, b"", ON), Ok(()));
    assert_eq!(dsp.to_ascii(), MockDisplay::new(8, 8).to_ascii());
}

#[test]
fn drawing_far_off_screen_does_not_panic() {
    let mut dsp = MockDisplay::new(8, 8);
    assert_eq!(
        draw_text(&mut dsp, &FONT6X8, i16::MAX - 1, i16::MIN + 1, b"AAAA", ON),
        Err(Error::OutOfBounds)
    );
    assert_eq!(dsp.to_ascii(), MockDisplay::new(8, 8).to_ascii());
}

#[test]
fn matches_the_c_host_test_golden_picture() {
    // Byte-for-byte the expectation of `test_display_text_draw()` in
    // `test/host/Platinenmacher/test_display/test.c`: two draws of the 8x8
    // font into a 20x20 framebuffer, at (1,1) and (1,10).
    let mut dsp = MockDisplay::new(20, 20);
    assert_eq!(draw_text(&mut dsp, &FONT8X8, 1, 1, b"AB", ON), Ok(()));
    assert_eq!(draw_text(&mut dsp, &FONT8X8, 1, 10, b"CD", ON), Ok(()));

    assert_eq!(
        dsp.to_ascii(),
        concat!(
            "....................\n",
            "...##....######.....\n",
            "..####....##..##....\n",
            ".##..##...##..##....\n",
            ".##..##...#####.....\n",
            ".######...##..##....\n",
            ".##..##...##..##....\n",
            ".##..##..######.....\n",
            "....................\n",
            "....................\n",
            "...####..#####......\n",
            "..##..##..##.##.....\n",
            ".##.......##..##....\n",
            ".##.......##..##....\n",
            ".##.......##..##....\n",
            "..##..##..##.##.....\n",
            "...####..#####......\n",
            "....................\n",
            "....................\n",
            "...................."
        )
    );

    // The C test compares the raw framebuffer, so do that too: every set pixel
    // carries the colour that was asked for and nothing else was touched.
    assert_eq!(dsp.framebuffer().len(), 20 * 20);
    assert!(dsp.framebuffer().iter().all(|&p| p == 0 || p == ON));
}
