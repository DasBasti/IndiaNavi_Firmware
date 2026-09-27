//! Host tests for the NMEA parser.
//!
//! `test/embedded/test_nmea_parser/test.c` only asserted that
//! `nmea_parser_init()` returned non-NULL, because everything else needed a
//! UART and a live receiver. That single assertion is ported as
//! `parser_starts_empty`; the rest of this file is what the C test could not
//! do on a host.

use nmea::{Event, GpsDate, GpsFix, GpsFixMode, GpsTime, ParseError, Parser, SentenceKind};

const GGA: &str = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n";
const GGA_SOUTH_WEST: &str =
    "$GPGGA,092725.00,4717.11399,S,00833.91590,W,1,08,1.01,499.6,M,48.0,M,,*54\r\n";
const GSA: &str = "$GPGSA,A,3,04,05,06,09,12,24,25,29,31,32,,,2.5,1.3,2.1*30\r\n";
const GSV_1: &str = "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75\r\n";
const GSV_2: &str = "$GPGSV,2,2,08,15,60,150,49,17,35,050,42,19,05,010,30,24,80,270,50*78\r\n";
const RMC: &str = "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*10\r\n";
const GLL: &str = "$GPGLL,4916.45,N,12311.12,W,225444,A,*1D\r\n";
const VTG: &str = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n";
const TXT: &str = "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50\r\n";

/// A parser that updates on every accepted sentence, so single-sentence tests
/// do not have to feed all six types.
fn parser_for(kind: SentenceKind) -> Parser {
    let mut parser = Parser::new();
    parser.set_required_sentences(&[kind]);
    parser
}

fn feed_str(parser: &mut Parser, sentence: &str) -> Vec<Event> {
    parser.feed(sentence.as_bytes())
}

fn assert_close(actual: f32, expected: f32, what: &str) {
    assert!(
        (actual - expected).abs() < 1e-4,
        "{what}: expected {expected}, got {actual}"
    );
}

fn errors(events: &[Event]) -> Vec<ParseError> {
    events
        .iter()
        .filter_map(|event| match event {
            Event::Error(error) => Some(*error),
            _ => None,
        })
        .collect()
}

// --- ported from test/embedded/test_nmea_parser/test.c ---------------------

#[test]
fn parser_starts_empty() {
    let parser = Parser::new();
    assert_eq!(*parser.data(), nmea::GpsData::default());
    assert_eq!(parser.data().fix, GpsFix::Invalid);
    assert_eq!(parser.data().fix_mode, GpsFixMode::Invalid);
    assert!(!parser.data().valid);
}

// --- sentence types --------------------------------------------------------

#[test]
fn parses_gga() {
    let mut parser = parser_for(SentenceKind::Gga);
    let events = feed_str(&mut parser, GGA);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Gga));
    assert!(matches!(events[1], Event::Update(_)));

    let data = parser.data();
    assert_eq!(
        data.tim,
        GpsTime {
            hour: 9,
            minute: 27,
            second: 25,
            thousand: 0,
        }
    );
    assert_close(data.latitude, 47.0 + 17.11399 / 60.0, "latitude");
    assert_close(data.longitude, 8.0 + 33.9159 / 60.0, "longitude");
    assert_eq!(data.fix, GpsFix::Gps);
    assert_eq!(data.sats_in_use, 8);
    assert_close(data.dop_h, 1.01, "hdop");
    // As in C: antenna altitude plus geoid separation.
    assert_close(data.altitude, 499.6 + 48.0, "altitude");
}

#[test]
fn gga_hemisphere_letters_flip_the_sign() {
    let mut parser = parser_for(SentenceKind::Gga);
    feed_str(&mut parser, GGA_SOUTH_WEST);
    assert_close(
        parser.data().latitude,
        -(47.0 + 17.11399 / 60.0),
        "latitude",
    );
    assert_close(
        parser.data().longitude,
        -(8.0 + 33.9159 / 60.0),
        "longitude",
    );
}

#[test]
fn parses_gsa() {
    let mut parser = parser_for(SentenceKind::Gsa);
    let events = feed_str(&mut parser, GSA);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Gsa));

    let data = parser.data();
    assert_eq!(data.fix_mode, GpsFixMode::Fix3D);
    assert_eq!(
        data.sats_id_in_use,
        [4, 5, 6, 9, 12, 24, 25, 29, 31, 32, 0, 0]
    );
    assert_close(data.dop_p, 2.5, "pdop");
    assert_close(data.dop_h, 1.3, "hdop");
    assert_close(data.dop_v, 2.1, "vdop");
}

#[test]
fn parses_gsv_group() {
    let mut parser = parser_for(SentenceKind::Gsv);

    // The first message of the group is accepted but does not complete it.
    let events = feed_str(&mut parser, GSV_1);
    assert_eq!(events, vec![Event::Sentence(SentenceKind::Gsv)]);
    assert_eq!(parser.data().sats_in_view, 8);

    let events = feed_str(&mut parser, GSV_2);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Gsv));
    assert!(
        matches!(events[1], Event::Update(_)),
        "the last message of a GSV group completes it"
    );

    let sats = parser.data().sats_desc_in_view;
    assert_eq!(sats[0].num, 1);
    assert_eq!(sats[0].elevation, 40);
    assert_eq!(sats[0].azimuth, 83);
    assert_eq!(sats[0].snr, 46);
    assert_eq!(sats[3].num, 14);
    assert_eq!(sats[3].snr, 45);
    // Second message fills slots 4..7.
    assert_eq!(sats[4].num, 15);
    assert_eq!(sats[4].azimuth, 150);
    assert_eq!(sats[7].num, 24);
    assert_eq!(sats[7].elevation, 80);
    assert_eq!(sats[7].azimuth, 270);
    assert_eq!(sats[7].snr, 50);
}

#[test]
fn parses_rmc() {
    let mut parser = parser_for(SentenceKind::Rmc);
    feed_str(&mut parser, RMC);

    let data = parser.data();
    assert_eq!(
        data.tim,
        GpsTime {
            hour: 16,
            minute: 12,
            second: 29,
            thousand: 487,
        }
    );
    assert!(data.valid);
    assert_close(data.latitude, 37.0 + 23.2475 / 60.0, "latitude");
    assert_close(data.longitude, -(121.0 + 58.3416 / 60.0), "longitude");
    // Same scaling as the C parser: knots * 1.852.
    assert_close(data.speed, 0.13 * 1.852, "speed");
    assert_close(data.cog, 309.62, "cog");
    assert_eq!(
        data.date,
        GpsDate {
            day: 12,
            month: 5,
            year: 98,
        }
    );
}

#[test]
fn parses_gll() {
    let mut parser = parser_for(SentenceKind::Gll);
    feed_str(&mut parser, GLL);

    let data = parser.data();
    assert_close(data.latitude, 49.0 + 16.45 / 60.0, "latitude");
    assert_close(data.longitude, -(123.0 + 11.12 / 60.0), "longitude");
    assert_eq!(
        data.tim,
        GpsTime {
            hour: 22,
            minute: 54,
            second: 44,
            thousand: 0,
        }
    );
    assert!(data.valid);
}

#[test]
fn parses_vtg() {
    let mut parser = parser_for(SentenceKind::Vtg);
    feed_str(&mut parser, VTG);

    let data = parser.data();
    assert_close(data.cog, 54.7, "cog");
    assert_close(data.variation, 34.4, "variation");
    // Field 7 (km/h) is parsed after field 5 (knots) and wins, as in C.
    assert_close(data.speed, 10.2 / 3.6, "speed");
}

#[test]
fn empty_fields_are_zero_like_strtof() {
    let mut parser = parser_for(SentenceKind::Gga);
    let events = feed_str(&mut parser, "$GPGGA,,,,,,0,00,,,M,,M,,*66\r\n");
    assert!(errors(&events).is_empty(), "{events:?}");
    assert_eq!(parser.data().fix, GpsFix::Invalid);
    assert_eq!(parser.data().latitude, 0.0);
    assert_eq!(parser.data().altitude, 0.0);
}

// --- update bookkeeping ----------------------------------------------------

#[test]
fn update_fires_once_all_required_sentences_are_seen() {
    let mut parser = Parser::new(); // requires all six
    let mut updates = 0;
    for sentence in [GGA, GSA, GSV_1, GSV_2, RMC, GLL] {
        for event in feed_str(&mut parser, sentence) {
            assert!(!matches!(event, Event::Update(_)), "too early: {sentence}");
            updates += usize::from(matches!(event, Event::Update(_)));
        }
    }
    assert_eq!(updates, 0);

    let events = feed_str(&mut parser, VTG);
    let Some(Event::Update(snapshot)) = events.last().cloned() else {
        panic!("expected an update, got {events:?}");
    };
    assert_eq!(snapshot, *parser.data());
    assert_eq!(snapshot.sats_in_use, 8);
    assert!(snapshot.valid);

    // The mask is cleared, so one more sentence does not fire again.
    let events = feed_str(&mut parser, GGA);
    assert_eq!(events, vec![Event::Sentence(SentenceKind::Gga)]);
}

#[test]
fn unknown_sentences_are_reported_not_rejected() {
    let mut parser = Parser::new();
    let events = feed_str(&mut parser, TXT);
    assert_eq!(events, vec![Event::Unknown("$GPTXT".to_string())]);
}

// --- checksum validation ---------------------------------------------------

#[test]
fn rejects_wrong_checksum() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let broken = RMC.replace("*10", "*11");
    let events = feed_str(&mut parser, &broken);
    assert_eq!(
        events,
        vec![Event::Error(ParseError::Checksum {
            computed: 0x10,
            found: 0x11,
        })]
    );
    // Nothing from a rejected sentence counts towards an update.
    assert!(!parser.data().valid);
}

#[test]
fn rejects_missing_and_malformed_checksum() {
    for sentence in [
        "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,\r\n", // no '*'
        "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*1\r\n", // one digit
        "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*ZZ\r\n", // not hex
        "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*\r\n", // empty
    ] {
        let mut parser = parser_for(SentenceKind::Rmc);
        let events = feed_str(&mut parser, sentence);
        assert_eq!(
            events,
            vec![Event::Error(ParseError::MalformedChecksum)],
            "{sentence}"
        );
    }
}

#[test]
fn a_valid_sentence_after_a_rejected_one_still_parses() {
    let mut parser = parser_for(SentenceKind::Rmc);
    feed_str(&mut parser, &RMC.replace("*10", "*11"));
    let events = feed_str(&mut parser, RMC);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Rmc));
    assert!(parser.data().valid);
}

// --- malformed sentence rejection -----------------------------------------

#[test]
fn rejects_non_numeric_field() {
    let mut parser = parser_for(SentenceKind::Gga);
    let events = feed_str(
        &mut parser,
        "$GPGGA,092725.00,4717.11399,N,00833.91590,E,X,08,1.01,499.6,M,48.0,M,,*32\r\n",
    );
    assert_eq!(
        events,
        vec![Event::Error(ParseError::InvalidField {
            statement: SentenceKind::Gga,
            index: 6,
        })]
    );
    assert_eq!(parser.data().fix, GpsFix::Invalid);
}

#[test]
fn rejects_malformed_time_and_date() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let events = feed_str(
        &mut parser,
        "$GPRMC,991229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*17\r\n",
    );
    assert_eq!(events, vec![Event::Error(ParseError::InvalidTime)]);

    let events = feed_str(
        &mut parser,
        "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,991398,,*14\r\n",
    );
    assert_eq!(events, vec![Event::Error(ParseError::InvalidDate)]);
}

#[test]
fn rejects_overlong_field() {
    let mut parser = parser_for(SentenceKind::Gga);
    let events = feed_str(&mut parser, "$GPGGA,09272500000000000000,*00\r\n");
    assert_eq!(
        events,
        vec![Event::Error(ParseError::ItemTooLong { index: 1 })]
    );
}

#[test]
fn rejects_overlong_sentence() {
    let mut parser = parser_for(SentenceKind::Gga);
    // Every field is empty, so only the length can make this fail.
    let mut sentence = String::from("$GPGGA");
    while sentence.len() < 200 {
        sentence.push(',');
    }
    sentence.push_str("*00\r\n");
    let events = feed_str(&mut parser, &sentence);
    assert_eq!(events, vec![Event::Error(ParseError::SentenceTooLong)]);
}

#[test]
fn rejects_binary_garbage_inside_a_sentence() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let mut bytes = RMC.as_bytes().to_vec();
    bytes.insert(10, 0xff);
    let events = parser.feed(&bytes);
    assert_eq!(
        events,
        vec![Event::Error(ParseError::InvalidCharacter { byte: 0xff })]
    );
}

#[test]
fn noise_before_the_first_dollar_is_ignored() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let mut bytes = vec![0x00, 0xff, b'g', b'a', b'r', b'b', b'a', b'g', b'e'];
    bytes.extend_from_slice(RMC.as_bytes());
    let events = parser.feed(&bytes);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Rmc));
}

#[test]
fn a_truncated_sentence_does_not_poison_the_next_one() {
    let mut parser = parser_for(SentenceKind::Rmc);
    // Receiver output cut off mid-sentence, no terminator.
    let events = parser.feed(b"$GPRMC,161229.487,A,3723.24");
    assert!(events.is_empty(), "{events:?}");
    let events = feed_str(&mut parser, RMC);
    assert_eq!(events[0], Event::Sentence(SentenceKind::Rmc));
    assert_close(parser.data().cog, 309.62, "cog");
}

// --- chunk boundaries ------------------------------------------------------

#[test]
fn byte_at_a_time_parses_the_same_as_one_chunk() {
    let stream = [GGA, GSA, GSV_1, GSV_2, RMC, GLL, VTG].concat();

    let mut whole = Parser::new();
    let whole_events = whole.feed(stream.as_bytes());

    let mut split = Parser::new();
    let mut split_events = Vec::new();
    for byte in stream.as_bytes() {
        split_events.extend(split.feed(&[*byte]));
    }

    assert_eq!(split_events, whole_events);
    assert_eq!(split.data(), whole.data());
    assert!(whole_events.iter().any(|e| matches!(e, Event::Update(_))));
}

#[test]
fn every_split_point_parses_the_same() {
    let stream = [RMC, GGA, GSV_1].concat();
    let mut whole = Parser::new();
    let expected_events = whole.feed(stream.as_bytes());
    let expected_data = *whole.data();

    for split_at in 0..stream.len() {
        let mut parser = Parser::new();
        let mut events = parser.feed(&stream.as_bytes()[..split_at]);
        events.extend(parser.feed(&stream.as_bytes()[split_at..]));
        assert_eq!(events, expected_events, "split at {split_at}");
        assert_eq!(*parser.data(), expected_data, "split at {split_at}");
    }
}

#[test]
fn a_sentence_split_inside_a_field_still_parses() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let (head, tail) = RMC.split_at(RMC.find("3723.2475").unwrap() + 4);
    assert!(parser.feed(head.as_bytes()).is_empty());
    let events = parser.feed(tail.as_bytes());
    assert_eq!(events[0], Event::Sentence(SentenceKind::Rmc));
    assert_close(parser.data().latitude, 37.0 + 23.2475 / 60.0, "latitude");
}

#[test]
fn feed_with_reports_the_same_events_as_feed() {
    let mut a = Parser::new();
    let mut b = Parser::new();
    let stream = [GGA, RMC].concat();
    let from_feed = a.feed(stream.as_bytes());
    let mut from_callback = Vec::new();
    b.feed_with(stream.as_bytes(), |event| from_callback.push(event));
    assert_eq!(from_feed, from_callback);
}

#[test]
fn reset_clears_data_but_keeps_the_required_set() {
    let mut parser = parser_for(SentenceKind::Rmc);
    feed_str(&mut parser, RMC);
    assert!(parser.data().valid);
    parser.reset();
    assert_eq!(*parser.data(), nmea::GpsData::default());
    let events = feed_str(&mut parser, RMC);
    assert!(matches!(events.last(), Some(Event::Update(_))));
}

#[test]
fn lone_newline_terminator_is_accepted() {
    let mut parser = parser_for(SentenceKind::Rmc);
    let events = feed_str(&mut parser, RMC.trim_end_matches("\r\n"));
    assert!(events.is_empty());
    let events = parser.feed(b"\n");
    assert_eq!(events[0], Event::Sentence(SentenceKind::Rmc));
}
