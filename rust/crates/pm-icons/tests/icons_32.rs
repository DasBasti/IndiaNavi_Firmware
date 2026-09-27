//! Checks the generated icon data against the shape declared by
//! `lib/icons_32/icons_32.h` and against bytes transcribed from the C assets.

use pm_icons::{icons_32, Icon, BITS_PER_PIXEL, ICON_SIZE};

/// Every `extern uint8_t ...[]` in `lib/icons_32/icons_32.h`.
const C_SYMBOLS: &[&str] = &[
    "bat_100",
    "bat_80",
    "bat_50",
    "bat_30",
    "bat_10",
    "bat_0",
    "GPS",
    "GPS_lock",
    "GPS_search",
    "noGPS",
    "path",
    "SD",
    "noSD",
    "norden",
    "WIFI_0",
    "WIFI_1",
    "WIFI_2",
    "WIFI_3",
];

#[test]
fn every_c_symbol_has_a_const() {
    for symbol in C_SYMBOLS {
        assert!(
            icons_32::ALL.iter().any(|(name, _)| name == symbol),
            "no const for C symbol `{symbol}`"
        );
    }
    assert_eq!(
        icons_32::ALL.len(),
        C_SYMBOLS.len(),
        "ALL and icons_32.h disagree on how many icons there are"
    );
}

#[test]
fn data_length_matches_width_times_height_times_bpp() {
    for (name, icon) in icons_32::ALL {
        let expected = icon.width as usize * icon.height as usize * BITS_PER_PIXEL as usize / 8;
        assert_eq!(
            icon.data.len(),
            expected,
            "`{name}` has {} bytes, expected {expected} for {}x{} at {BITS_PER_PIXEL}bpp",
            icon.data.len(),
            icon.width,
            icon.height
        );
        assert_eq!(icon.expected_data_len(), expected, "`{name}`");
    }
}

#[test]
fn every_icon_is_icon_size_square() {
    for (name, icon) in icons_32::ALL {
        assert_eq!(icon.width, ICON_SIZE, "`{name}` width");
        assert_eq!(icon.height, ICON_SIZE, "`{name}` height");
        assert_eq!(icon.data.len(), 512, "`{name}` data length");
    }
}

#[test]
fn pixels_are_three_bit_colour_codes() {
    // Two pixels per byte, each a 3-bit ACeP colour code, so neither nibble
    // may have its top bit set.
    for (name, icon) in icons_32::ALL {
        for (i, byte) in icon.data.iter().enumerate() {
            assert_eq!(
                byte & 0x88,
                0,
                "`{name}` byte {i} is {byte:#04x}, which is not two colour codes"
            );
        }
    }
}

#[test]
fn icons_are_distinct() {
    for (i, (name, icon)) in icons_32::ALL.iter().enumerate() {
        for (other_name, other) in &icons_32::ALL[i + 1..] {
            assert_ne!(
                icon.data, other.data,
                "`{name}` and `{other_name}` have identical data"
            );
        }
        assert!(
            !icons_32::ALL[i + 1..].iter().any(|(n, _)| n == name),
            "`{name}` is listed twice"
        );
    }
}

/// Rows transcribed by hand from the C assets. These pin the byte data down so
/// a broken regeneration cannot pass silently.
#[test]
fn rows_match_the_c_assets() {
    // lib/icons_32/GPS.png.c, first and sixteenth row.
    assert_row(
        "GPS",
        &icons_32::GPS,
        0,
        &[
            0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x70, 0x07, 0x77, 0x77, 0x77, 0x77, 0x77,
            0x77, 0x77,
        ],
    );
    assert_row(
        "GPS",
        &icons_32::GPS,
        15,
        &[
            0x00, 0x00, 0x00, 0x07, 0x77, 0x77, 0x00, 0x77, 0x77, 0x00, 0x77, 0x77, 0x70, 0x00,
            0x00, 0x00,
        ],
    );
    // lib/icons_32/bat_100.png.c, second row.
    assert_row(
        "bat_100",
        &icons_32::BAT_100,
        1,
        &[
            0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x70, 0x00, 0x00, 0x07, 0x77, 0x77, 0x77, 0x77,
            0x77, 0x77,
        ],
    );
    // lib/icons_32/noSD.c, ninth row — the one row with non-monochrome codes.
    assert_row(
        "noSD",
        &icons_32::NO_SD,
        8,
        &[
            0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x44, 0x44, 0x66,
            0x07, 0x77,
        ],
    );
    // lib/icons_32/norden.c, first row.
    assert_row(
        "norden",
        &icons_32::NORDEN,
        0,
        &[
            0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x70, 0x07, 0x77, 0x77, 0x77, 0x77, 0x77,
            0x77, 0x77,
        ],
    );
}

fn assert_row(name: &str, icon: &Icon, row: usize, expected: &[u8]) {
    let bytes_per_row = icon.width as usize * BITS_PER_PIXEL as usize / 8;
    assert_eq!(expected.len(), bytes_per_row, "`{name}` row {row} fixture");
    let start = row * bytes_per_row;
    assert_eq!(
        &icon.data[start..start + bytes_per_row],
        expected,
        "`{name}` row {row}"
    );
}

#[test]
fn consts_are_reachable_from_the_crate_root() {
    // pm-gui wraps these, so the re-export has to stay.
    let icon: Icon = pm_icons::GPS_LOCK;
    assert_eq!(icon, icons_32::GPS_LOCK);
    assert_eq!(pm_icons::WIFI_3.data.len(), 512);
    assert_eq!(pm_icons::NO_GPS.width, ICON_SIZE);
}
