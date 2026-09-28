//! Tests for the port of `convert_umlauts_inplace()` in `lib/helper/umlaut.c`.
//!
//! These assert the mapping the C code actually implements, including its two
//! bugs, so that changing the behaviour is a deliberate, visible act.

use pm_core::MockDisplay;
use pm_font::draw_text;
use pm_font::fonts::FONT6X8;
use pm_font::umlaut::convert_in_place;

fn folded(input: &str) -> Vec<u8> {
    let mut buf = input.as_bytes().to_vec();
    convert_in_place(&mut buf);
    buf
}

#[test]
fn a_umlaut_folds_to_ae() {
    assert_eq!(folded("ä"), b"ae");
    assert_eq!(folded("Bäcker"), b"Baecker");
}

#[test]
fn o_and_u_umlauts_fold_to_ae_too() {
    // Bug-for-bug with umlaut.c: 0xC3 0xB6 and 0xC3 0xBC both write 'a','e'
    // instead of "oe" and "ue".
    assert_eq!(folded("ö"), b"ae");
    assert_eq!(folded("ü"), b"ae");
    assert_eq!(folded("Köln"), b"Kaeln");
    assert_eq!(folded("Tür"), b"Taer");
}

#[test]
fn sharp_s_is_not_folded() {
    // umlaut.c tests for 0xC3 0x2C, but UTF-8 encodes 'ß' as 0xC3 0x9F, so the
    // "sz" branch is unreachable and the byte pair is left alone.
    assert_eq!(folded("ß"), "ß".as_bytes());
    assert_eq!(folded("Straße"), "Straße".as_bytes());
}

#[test]
fn the_dead_sz_branch_still_fires_for_the_byte_pair_it_names() {
    // Fed the exact sequence the C code looks for, the port behaves the same.
    let mut buf = [0xc3u8, 0x2c];
    convert_in_place(&mut buf);
    assert_eq!(&buf, b"sz");
}

#[test]
fn ascii_and_other_latin1_characters_pass_through() {
    assert_eq!(folded("Hello, World!"), b"Hello, World!");
    assert_eq!(folded("é"), "é".as_bytes()); // 0xC3 0xA9, no mapping
    assert_eq!(folded(""), b"");
}

#[test]
fn conversion_stops_at_a_nul_terminator() {
    let mut buf = *b"a\0\xc3\xa4";
    convert_in_place(&mut buf);
    assert_eq!(&buf, b"a\0\xc3\xa4");
}

#[test]
fn a_trailing_lead_byte_does_not_read_past_the_buffer() {
    // The C loop skips over the terminator here and reads on; this port stops.
    let mut buf = [0xc3u8];
    convert_in_place(&mut buf);
    assert_eq!(&buf, &[0xc3u8]);
}

#[test]
fn folding_is_length_preserving_so_it_works_in_place() {
    for word in ["ä", "ö", "ü", "Fußgängerzone", "Öl"] {
        assert_eq!(folded(word).len(), word.len(), "{word}");
    }
}

#[test]
fn folded_text_renders_as_two_ascii_glyphs() {
    // The same code path as gui/label.c: fold first, then draw.
    let mut buf = "ä".as_bytes().to_vec();
    convert_in_place(&mut buf);

    let mut folded_dsp = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut folded_dsp, &FONT6X8, 0, 0, &buf, 1), Ok(()));

    let mut ascii_dsp = MockDisplay::new(16, 8);
    assert_eq!(draw_text(&mut ascii_dsp, &FONT6X8, 0, 0, b"ae", 1), Ok(()));

    assert_eq!(folded_dsp.to_ascii(), ascii_dsp.to_ascii());

    // The raw UTF-8 bytes have no glyphs at all, which is why label.c folds.
    let mut raw_dsp = MockDisplay::new(16, 8);
    assert_eq!(
        draw_text(&mut raw_dsp, &FONT6X8, 0, 0, "ä".as_bytes(), 1),
        Ok(())
    );
    assert_eq!(raw_dsp.to_ascii(), MockDisplay::new(16, 8).to_ascii());
}
