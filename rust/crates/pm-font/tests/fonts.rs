//! Proves the generated Rust font tables are byte-identical to the C tables.
//!
//! `rust/tools/convert-fonts.mjs` produces `src/fonts/`; this test re-parses
//! the very same C sources at compile time and compares, so the conversion is
//! checked by `cargo test` alone, without node in the loop.

use pm_font::fonts::*;
use pm_font::{Font, Rotation};

/// Extracts the byte initialiser of `const uint8_t <symbol>[] = { ... };`.
fn c_array_bytes(src: &str, symbol: &str) -> Vec<u8> {
    let decl = src
        .find(&format!("{symbol}[] ="))
        .unwrap_or_else(|| panic!("{symbol}[] not found"));
    let rest = &src[decl..];
    let open = rest.find('{').expect("opening brace");
    let body = &rest[open + 1..];

    let mut depth = 1usize;
    let mut end = body.len();
    for (i, ch) in body.char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    end = i;
                    break;
                }
            }
            _ => {}
        }
    }

    let bytes = &body.as_bytes()[..end];
    let mut code = String::new();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'/' && bytes.get(i + 1) == Some(&b'/') {
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
        } else if bytes[i] == b'/' && bytes.get(i + 1) == Some(&b'*') {
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                i += 1;
            }
            i = (i + 2).min(bytes.len());
        } else {
            code.push(bytes[i] as char);
            i += 1;
        }
    }

    code.split(',')
        .map(str::trim)
        .filter(|tok| !tok.is_empty())
        .map(
            |tok| match tok.strip_prefix("0x").or_else(|| tok.strip_prefix("0X")) {
                Some(hex) => u8::from_str_radix(hex, 16).expect("hex byte"),
                None => tok.parse::<u8>().expect("decimal byte"),
            },
        )
        .collect()
}

fn check(font: &Font, raw: &[u8], c_source: &str, symbol: &str, width: u8, height: u8) {
    let expected = c_array_bytes(c_source, symbol);
    assert_eq!(
        raw.len(),
        expected.len(),
        "{symbol}: table length differs from the C source"
    );
    assert_eq!(
        raw,
        expected.as_slice(),
        "{symbol}: table bytes differ from the C source"
    );

    // The header the C `font_load_from_array()` reads back out.
    assert_eq!(font.width(), width, "{symbol}: width");
    assert_eq!(font.height(), height, "{symbol}: height");
    assert_eq!(font.width(), expected[0]);
    assert_eq!(font.height(), expected[1]);
    assert_eq!(font.first_char(), expected[2]);
    assert_eq!(font.rotation(), Rotation::from_u8(expected[3]));
    assert_eq!(font.data(), &expected[4..], "{symbol}: glyph data");
}

macro_rules! font_case {
    ($test:ident, $font:ident, $raw:ident, $file:literal, $symbol:literal, $w:expr, $h:expr) => {
        #[test]
        fn $test() {
            check(
                &$font,
                &$raw,
                include_str!(concat!("../../../../lib/Platinenmacher/fonts/", $file)),
                $symbol,
                $w,
                $h,
            );
        }
    };
}

font_case!(
    font6x8_matches_c,
    FONT6X8,
    FONT6X8_RAW,
    "font6x8.c",
    "font6x8",
    6,
    8
);
font_case!(
    font8x8_matches_c,
    FONT8X8,
    FONT8X8_RAW,
    "font8x8.c",
    "font8x8",
    8,
    8
);
font_case!(
    font8x16_matches_c,
    FONT8X16,
    FONT8X16_RAW,
    "font8x16.c",
    "font8x16",
    8,
    16
);
font_case!(
    font10x14_matches_c,
    FONT10X14,
    FONT10X14_RAW,
    "font10x14.h",
    "font10x14",
    10,
    14
);
font_case!(
    font12x8_matches_c,
    FONT12X8,
    FONT12X8_RAW,
    "font12x8.h",
    "font12x8",
    12,
    8
);
font_case!(
    font13x8_matches_c,
    FONT13X8,
    FONT13X8_RAW,
    "font13x8.h",
    "font13x8",
    13,
    8
);
font_case!(
    font16x8_matches_c,
    FONT16X8,
    FONT16X8_RAW,
    "font16x8.h",
    "font16x8",
    16,
    8
);
font_case!(
    font16x16_matches_c,
    FONT16X16,
    FONT16X16_RAW,
    "font16x16.h",
    "font16x16",
    16,
    16
);

#[test]
fn all_eight_fonts_are_exported() {
    assert_eq!(ALL_FONTS.len(), 8);
    let names: Vec<&str> = ALL_FONTS.iter().map(|f| f.name()).collect();
    assert_eq!(
        names,
        ["6x8", "8x8", "8x16", "10x14", "12x8", "13x8", "16x8", "16x16"]
    );
}

#[test]
fn glyph_lookup_is_bounded() {
    // Every font starts at the space character.
    for font in ALL_FONTS {
        assert_eq!(font.first_char(), 0x20, "{}", font.name());
        assert!(font.glyph(b' ').is_some(), "{}", font.name());
        assert!(font.glyph(b'~').is_some(), "{}", font.name());
        assert!(font.glyph(0x1f).is_none(), "{}", font.name());
        assert!(font.glyph(0xff).is_none(), "{}", font.name());
        assert_eq!(
            font.glyph(b' ').unwrap().len(),
            font.bytes_per_glyph(),
            "{}",
            font.name()
        );
        assert!(font.last_char() >= b'~', "{}", font.name());
    }
}

#[test]
fn glyph_stride_follows_the_c_formula() {
    // C quirk kept on purpose: `display_text_draw()` strides by
    // `width * height / 8`, which is 17 for the 10x14 table even though that
    // table really stores 20 bytes per glyph.
    assert_eq!(FONT8X8.bytes_per_glyph(), 8);
    assert_eq!(FONT6X8.bytes_per_glyph(), 6);
    assert_eq!(FONT16X16.bytes_per_glyph(), 32);
    assert_eq!(FONT10X14.bytes_per_glyph(), 17);
    assert_eq!(FONT10X14.data().len(), 1920);
}

#[test]
fn text_measurement_matches_font_c() {
    // font_text_pixel_width() == font->width * font_strlen(text)
    assert_eq!(FONT8X8.text_pixel_width(b"Hello"), 40);
    assert_eq!(FONT6X8.text_pixel_width(b"Hello"), 30);
    assert_eq!(FONT16X16.text_pixel_width(b""), 0);
    // font_strlen() stops at the NUL terminator.
    assert_eq!(FONT8X8.text_pixel_width(b"Hi\0gnored"), 16);
    assert_eq!(pm_font::strlen(b"Hi\0gnored"), 2);
    assert_eq!(pm_font::strlen(b"no terminator"), 13);
    // font_text_pixel_height() is just font->height
    assert_eq!(FONT8X8.text_pixel_height(), 8);
    assert_eq!(FONT10X14.text_pixel_height(), 14);
}
