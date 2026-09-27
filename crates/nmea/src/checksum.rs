/*
 * NMEA checksum helpers
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! NMEA checksum helpers.

/// NMEA checksum: the XOR of every payload byte between `$` and `*`.
///
/// This is the same accumulation `gps_decode()` performs in `nmea_parser.c`,
/// which XORs each character of the sentence but neither the leading `$` nor
/// the terminating `*`.
pub fn checksum(payload: &[u8]) -> u8 {
    let mut crc = 0u8;
    for b in payload {
        crc ^= *b;
    }
    crc
}

/// Checks the `*XX` checksum of a complete sentence such as
/// `"$PMTK161,0*28\r\n"`.
///
/// Returns `false` when the sentence has no `$`, no `*`, or a checksum that
/// is not two hex digits.
pub fn verify(sentence: &str) -> bool {
    let body = match sentence.strip_prefix('$') {
        Some(body) => body,
        None => return false,
    };
    let (payload, rest) = match body.split_once('*') {
        Some(split) => split,
        None => return false,
    };
    let digits = match rest.get(..2) {
        Some(digits) => digits,
        None => return false,
    };
    match u8::from_str_radix(digits, 16) {
        Ok(expected) => expected == checksum(payload.as_bytes()),
        Err(_) => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checksum_is_the_xor_of_the_payload() {
        assert_eq!(checksum(b"PMTK161,0"), 0x28);
        assert_eq!(checksum(b""), 0);
    }

    #[test]
    fn verify_accepts_a_well_formed_sentence() {
        assert!(verify("$PMTK161,0*28\r\n"));
        assert!(verify("$PMTK161,0*28"));
    }

    #[test]
    fn verify_rejects_a_broken_sentence() {
        // The stray space of `L96_AIC_ENABLE` in `l96.h`.
        assert!(!verify("$PMTK 286,1*23\r\n"));
        // Wrong checksum, no `$`, no `*`, and a truncated checksum.
        assert!(!verify("$PMTK161,0*29\r\n"));
        assert!(!verify("PMTK161,0*28\r\n"));
        assert!(!verify("$PMTK161,0\r\n"));
        assert!(!verify("$PMTK161,0*2"));
        assert!(!verify("$PMTK161,0*ZZ"));
    }
}
