//! The sentence state machine.
//!
//! Port of `gps_decode()`, `parse_item()` and the `parse_xxx()` helpers in
//! `lib/nmea_parser/nmea_parser.c`. The C version was fed one complete line at
//! a time by the UART pattern interrupt; this one is fed arbitrary byte chunks
//! and keeps its state across calls, so a sentence may be split anywhere.

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::error::ParseError;
use crate::gps::{
    GpsData, GpsFix, GpsFixMode, GPS_MAX_SATELLITES_IN_USE, GPS_MAX_SATELLITES_IN_VIEW,
};

/// Maximum length of a single comma separated field.
/// (`NMEA_MAX_STATEMENT_ITEM_LENGTH` in the C header.)
pub const NMEA_MAX_ITEM_LEN: usize = 16;

/// Maximum length of a whole sentence, `$` to `*hh` inclusive.
///
/// NMEA 0183 caps a sentence at 82 characters; a little headroom is allowed for
/// receivers that overrun it, after which the sentence is rejected rather than
/// parsed into a truncated item buffer (which is what the C code did).
pub const NMEA_MAX_SENTENCE_LEN: usize = 120;

/// A sentence type this parser understands.
///
/// Port of the `nmea_statement_t` members that `platformio.ini` enabled
/// (`CONFIG_NMEA_STATEMENT_*`). `STATEMENT_PLUGIN` has no equivalent: the
/// PMTK/PQ plugins stay with the firmware that talks to those receivers.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SentenceKind {
    /// Global positioning system fix data.
    Gga,
    /// GNSS DOP and active satellites.
    Gsa,
    /// GNSS satellites in view.
    Gsv,
    /// Recommended minimum specific GNSS data.
    Rmc,
    /// Geographic position, latitude/longitude.
    Gll,
    /// Course over ground and ground speed.
    Vtg,
}

impl SentenceKind {
    /// All sentence types this parser understands.
    pub const ALL: [SentenceKind; 6] = [
        SentenceKind::Gga,
        SentenceKind::Gsa,
        SentenceKind::Gsv,
        SentenceKind::Rmc,
        SentenceKind::Gll,
        SentenceKind::Vtg,
    ];

    /// Recognise a sentence id such as `$GPGGA` or `$GNRMC`.
    fn from_id(id: &str) -> Option<SentenceKind> {
        if id.len() < 4 || !id.starts_with('$') {
            return None;
        }
        match &id[id.len() - 3..] {
            "GGA" => Some(SentenceKind::Gga),
            "GSA" => Some(SentenceKind::Gsa),
            "GSV" => Some(SentenceKind::Gsv),
            "RMC" => Some(SentenceKind::Rmc),
            "GLL" => Some(SentenceKind::Gll),
            "VTG" => Some(SentenceKind::Vtg),
            _ => None,
        }
    }

    /// Bit of this sentence in the "seen since last update" mask.
    /// (`1 << STATEMENT_xxx` in the C code.)
    fn bit(self) -> u8 {
        match self {
            SentenceKind::Gga => 1 << 0,
            SentenceKind::Gsa => 1 << 1,
            SentenceKind::Gsv => 1 << 2,
            SentenceKind::Rmc => 1 << 3,
            SentenceKind::Gll => 1 << 4,
            SentenceKind::Vtg => 1 << 5,
        }
    }
}

/// Something the parser noticed while consuming bytes.
///
/// Replaces the `ESP_NMEA_EVENT` event loop postings of the C parser:
/// `GPS_UPDATE` is [`Event::Update`], `GPS_UNKNOWN` is [`Event::Unknown`].
#[derive(Debug, Clone, PartialEq)]
pub enum Event {
    /// A complete, checksum-valid sentence of a known type was applied to the
    /// parser's [`GpsData`].
    Sentence(SentenceKind),
    /// Every required sentence has been seen since the last update; this is the
    /// snapshot the firmware should act on. (`GPS_UPDATE`)
    Update(GpsData),
    /// A complete, checksum-valid sentence whose type this parser does not
    /// handle; carries the sentence id, e.g. `"$GPTXT"`. (`GPS_UNKNOWN`)
    Unknown(String),
    /// A sentence was rejected. The C parser dropped these silently.
    Error(ParseError),
}

/// Push-bytes NMEA parser.
///
/// The parser owns one [`GpsData`], like `esp_gps_t::parent` did. A sentence
/// updates it only once it has been received whole and its checksum checked
/// out, so a corrupted sentence cannot leave half its fields behind.
#[derive(Debug, Clone)]
pub struct Parser {
    /// Last fully accepted state. Only a sentence that passed its checksum
    /// reaches this: the C parser applied every field the moment it was
    /// parsed, so a corrupted sentence corrupted the shared struct.
    data: GpsData,
    /// Fields of the sentence being parsed, committed to `data` at its end.
    staging: GpsData,
    /// Current field, always valid ASCII.
    item: [u8; NMEA_MAX_ITEM_LEN],
    item_len: usize,
    /// Zero-based field index; field 0 is the sentence id.
    item_num: u8,
    /// Sentence id of the sentence being parsed, for [`Event::Unknown`].
    id: [u8; NMEA_MAX_ITEM_LEN],
    id_len: usize,
    /// Number of characters consumed since `$`.
    sentence_len: usize,
    in_sentence: bool,
    asterisk: bool,
    crc: u8,
    current: Option<SentenceKind>,
    /// First error hit in the current sentence; emitted at its end.
    pending_error: Option<ParseError>,
    /// GSV group bookkeeping (`esp_gps_t::sat_count` / `sat_num`).
    sat_count: u8,
    sat_num: u8,
    /// Sentences seen since the last [`Event::Update`]. (`parsed_statement`)
    parsed: u8,
    /// Sentences that must be seen before an update fires. (`all_statements`)
    required: u8,
}

impl Default for Parser {
    fn default() -> Self {
        Self::new()
    }
}

impl Parser {
    /// A parser that requires all six supported sentence types before it emits
    /// an [`Event::Update`], matching the `CONFIG_NMEA_STATEMENT_*` set that
    /// `platformio.ini` enabled.
    pub fn new() -> Self {
        Parser {
            data: GpsData::default(),
            staging: GpsData::default(),
            item: [0; NMEA_MAX_ITEM_LEN],
            item_len: 0,
            item_num: 0,
            id: [0; NMEA_MAX_ITEM_LEN],
            id_len: 0,
            sentence_len: 0,
            in_sentence: false,
            asterisk: false,
            crc: 0,
            current: None,
            pending_error: None,
            sat_count: 0,
            sat_num: 0,
            parsed: 0,
            required: SentenceKind::ALL.iter().fold(0, |m, k| m | k.bit()),
        }
    }

    /// Choose which sentences must be seen before an [`Event::Update`] fires.
    ///
    /// This is the runtime form of the C `CONFIG_NMEA_STATEMENT_*` build flags:
    /// a receiver that never sends, say, GSV would otherwise never produce an
    /// update. An empty set makes every accepted sentence produce an update.
    pub fn set_required_sentences(&mut self, kinds: &[SentenceKind]) {
        self.required = kinds.iter().fold(0, |m, k| m | k.bit());
    }

    /// The data accumulated so far, whether or not an update has fired.
    pub fn data(&self) -> &GpsData {
        &self.data
    }

    /// Drop all parsed data and any half-consumed sentence.
    pub fn reset(&mut self) {
        let required = self.required;
        *self = Parser::new();
        self.required = required;
    }

    /// Feed received bytes, collecting the events they produced.
    ///
    /// Chunk boundaries are irrelevant: a sentence split across any number of
    /// calls parses the same as one passed whole.
    pub fn feed(&mut self, bytes: &[u8]) -> Vec<Event> {
        let mut events = Vec::new();
        self.feed_with(bytes, |event| events.push(event));
        events
    }

    /// Feed received bytes, handing each event to `on_event` as it happens.
    ///
    /// Allocation-free unless an [`Event::Unknown`] is produced.
    pub fn feed_with<F: FnMut(Event)>(&mut self, bytes: &[u8], mut on_event: F) {
        for &byte in bytes {
            self.push_byte(byte, &mut on_event);
        }
    }

    fn push_byte(&mut self, byte: u8, on_event: &mut dyn FnMut(Event)) {
        match byte {
            b'$' => {
                // A `$` always starts a new sentence, even mid-sentence: the
                // previous one was truncated by a lost chunk.
                if let Some(error) = self.pending_error.take() {
                    on_event(Event::Error(error));
                }
                self.start_sentence();
            }
            _ if !self.in_sentence => {
                // Noise before the first `$`; the C parser ignored it too.
            }
            b'\r' | b'\n' => self.finish_sentence(on_event),
            b',' => {
                self.count_char();
                self.end_item();
                self.crc ^= byte;
                self.next_item();
            }
            b'*' => {
                self.count_char();
                self.end_item();
                self.asterisk = true;
                self.next_item();
            }
            _ => {
                self.count_char();
                if !(0x20..=0x7e).contains(&byte) {
                    self.fail(ParseError::InvalidCharacter { byte });
                    return;
                }
                if !self.asterisk {
                    self.crc ^= byte;
                }
                self.push_item_char(byte);
            }
        }
    }

    fn start_sentence(&mut self) {
        self.staging = self.data;
        self.in_sentence = true;
        self.asterisk = false;
        self.item_num = 0;
        self.item_len = 0;
        self.id_len = 0;
        self.sentence_len = 1;
        self.crc = 0;
        self.current = None;
        self.pending_error = None;
        self.sat_count = 0;
        self.sat_num = 0;
        self.push_item_char(b'$');
    }

    fn count_char(&mut self) {
        self.sentence_len += 1;
        if self.sentence_len > NMEA_MAX_SENTENCE_LEN {
            self.fail(ParseError::SentenceTooLong);
        }
    }

    fn push_item_char(&mut self, byte: u8) {
        if self.item_len >= NMEA_MAX_ITEM_LEN {
            // Fields of sentences we do not parse (a long $GPTXT message, say)
            // are simply dropped; only an overlong field we would have read is
            // an error.
            if self.item_num == 0 || self.current.is_some() {
                self.fail(ParseError::ItemTooLong {
                    index: self.item_num,
                });
            }
            return;
        }
        self.item[self.item_len] = byte;
        self.item_len += 1;
    }

    fn next_item(&mut self) {
        self.item_len = 0;
        self.item_num = self.item_num.saturating_add(1);
    }

    /// Record the first error of the current sentence.
    fn fail(&mut self, error: ParseError) {
        if self.pending_error.is_none() {
            self.pending_error = Some(error);
        }
    }

    fn item_str(&self) -> &str {
        // Only printable ASCII is ever pushed, so this cannot fail.
        core::str::from_utf8(&self.item[..self.item_len]).unwrap_or("")
    }

    /// Port of `parse_item()`.
    fn end_item(&mut self) {
        if self.pending_error.is_some() {
            return;
        }
        let index = self.item_num;
        if index == 0 {
            self.id[..self.item_len].copy_from_slice(&self.item[..self.item_len]);
            self.id_len = self.item_len;
            self.current = SentenceKind::from_id(self.item_str());
            return;
        }
        let Some(kind) = self.current else {
            return;
        };
        let result = match kind {
            SentenceKind::Gga => self.parse_gga(index),
            SentenceKind::Gsa => self.parse_gsa(index),
            SentenceKind::Gsv => self.parse_gsv(index),
            SentenceKind::Rmc => self.parse_rmc(index),
            SentenceKind::Gll => self.parse_gll(index),
            SentenceKind::Vtg => self.parse_vtg(index),
        };
        if let Err(error) = result {
            self.fail(error);
        }
    }

    /// Port of the `'\r'` branch of `gps_decode()`.
    fn finish_sentence(&mut self, on_event: &mut dyn FnMut(Event)) {
        if !self.in_sentence {
            return;
        }
        self.in_sentence = false;

        if let Some(error) = self.pending_error.take() {
            on_event(Event::Error(error));
            return;
        }
        if !self.asterisk {
            on_event(Event::Error(ParseError::MalformedChecksum));
            return;
        }
        let Some(found) = parse_hex_u8(self.item_str()) else {
            on_event(Event::Error(ParseError::MalformedChecksum));
            return;
        };
        if found != self.crc {
            on_event(Event::Error(ParseError::Checksum {
                computed: self.crc,
                found,
            }));
            return;
        }
        let Some(kind) = self.current else {
            on_event(Event::Unknown(
                core::str::from_utf8(&self.id[..self.id_len])
                    .unwrap_or("")
                    .to_string(),
            ));
            return;
        };
        // The sentence is complete and intact: commit its fields.
        self.data = self.staging;
        on_event(Event::Sentence(kind));
        // A GSV group only counts once its last message arrived.
        if kind != SentenceKind::Gsv || (self.sat_num != 0 && self.sat_num == self.sat_count) {
            self.parsed |= kind.bit();
        }
        if self.parsed & self.required == self.required {
            self.parsed = 0;
            on_event(Event::Update(self.data));
        }
    }

    /// Port of `parse_gga()`.
    fn parse_gga(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Gga;
        match index {
            1 => self.staging.tim = parse_utc_time(self.item_str(), self.staging.tim)?,
            2 => self.staging.latitude = parse_lat_long(self.item_str(), kind, index)?,
            3 => self.staging.latitude *= hemisphere_sign(self.item_str(), b'S'),
            4 => self.staging.longitude = parse_lat_long(self.item_str(), kind, index)?,
            5 => self.staging.longitude *= hemisphere_sign(self.item_str(), b'W'),
            6 => self.staging.fix = GpsFix::from_u8(parse_u32(self.item_str(), kind, index)? as u8),
            7 => self.staging.sats_in_use = parse_u32(self.item_str(), kind, index)? as u8,
            8 => self.staging.dop_h = parse_f32(self.item_str(), kind, index)?,
            9 => self.staging.altitude = parse_f32(self.item_str(), kind, index)?,
            11 => self.staging.altitude += parse_f32(self.item_str(), kind, index)?,
            _ => {}
        }
        Ok(())
    }

    /// Port of `parse_gsa()`.
    fn parse_gsa(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Gsa;
        match index {
            2 => {
                self.staging.fix_mode =
                    GpsFixMode::from_u8(parse_u32(self.item_str(), kind, index)? as u8)
            }
            15 => self.staging.dop_p = parse_f32(self.item_str(), kind, index)?,
            16 => self.staging.dop_h = parse_f32(self.item_str(), kind, index)?,
            17 => self.staging.dop_v = parse_f32(self.item_str(), kind, index)?,
            3..=14 => {
                let id = parse_u32(self.item_str(), kind, index)? as u8;
                let slot = usize::from(index - 3);
                if slot < GPS_MAX_SATELLITES_IN_USE {
                    self.staging.sats_id_in_use[slot] = id;
                }
            }
            _ => {}
        }
        Ok(())
    }

    /// Port of `parse_gsv()`.
    fn parse_gsv(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Gsv;
        match index {
            1 => self.sat_count = parse_u32(self.item_str(), kind, index)? as u8,
            2 => self.sat_num = parse_u32(self.item_str(), kind, index)? as u8,
            3 => self.staging.sats_in_view = parse_u32(self.item_str(), kind, index)? as u8,
            4..=19 => {
                if self.sat_num == 0 {
                    return Ok(());
                }
                let field = index - 4; // normalise 4..=19 to 0..=15
                let slot = 4 * usize::from(self.sat_num - 1) + usize::from(field / 4);
                if slot >= GPS_MAX_SATELLITES_IN_VIEW {
                    return Ok(());
                }
                let value = parse_u32(self.item_str(), kind, index)?;
                let sat = &mut self.staging.sats_desc_in_view[slot];
                match field % 4 {
                    0 => sat.num = value as u8,
                    1 => sat.elevation = value as u8,
                    2 => sat.azimuth = value as u16,
                    _ => sat.snr = value as u8,
                }
            }
            _ => {}
        }
        Ok(())
    }

    /// Port of `parse_rmc()`.
    fn parse_rmc(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Rmc;
        match index {
            1 => self.staging.tim = parse_utc_time(self.item_str(), self.staging.tim)?,
            2 => self.staging.valid = self.item_str().as_bytes().first() == Some(&b'A'),
            3 => self.staging.latitude = parse_lat_long(self.item_str(), kind, index)?,
            4 => self.staging.latitude *= hemisphere_sign(self.item_str(), b'S'),
            5 => self.staging.longitude = parse_lat_long(self.item_str(), kind, index)?,
            6 => self.staging.longitude *= hemisphere_sign(self.item_str(), b'W'),
            // Scaling kept bit-for-bit from the C parser; see GpsData::speed.
            7 => self.staging.speed = parse_f32(self.item_str(), kind, index)? * 1.852,
            8 => self.staging.cog = parse_f32(self.item_str(), kind, index)?,
            9 => self.staging.date = parse_date(self.item_str(), self.staging.date)?,
            10 => self.staging.variation = parse_f32(self.item_str(), kind, index)?,
            _ => {}
        }
        Ok(())
    }

    /// Port of `parse_gll()`.
    fn parse_gll(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Gll;
        match index {
            1 => self.staging.latitude = parse_lat_long(self.item_str(), kind, index)?,
            2 => self.staging.latitude *= hemisphere_sign(self.item_str(), b'S'),
            3 => self.staging.longitude = parse_lat_long(self.item_str(), kind, index)?,
            4 => self.staging.longitude *= hemisphere_sign(self.item_str(), b'W'),
            5 => self.staging.tim = parse_utc_time(self.item_str(), self.staging.tim)?,
            6 => self.staging.valid = self.item_str().as_bytes().first() == Some(&b'A'),
            _ => {}
        }
        Ok(())
    }

    /// Port of `parse_vtg()`.
    fn parse_vtg(&mut self, index: u8) -> Result<(), ParseError> {
        let kind = SentenceKind::Vtg;
        match index {
            1 => self.staging.cog = parse_f32(self.item_str(), kind, index)?,
            3 => self.staging.variation = parse_f32(self.item_str(), kind, index)?,
            // Both scalings kept bit-for-bit from the C parser; see GpsData::speed.
            5 => self.staging.speed = parse_f32(self.item_str(), kind, index)? * 1.852,
            7 => self.staging.speed = parse_f32(self.item_str(), kind, index)? / 3.6,
            _ => {}
        }
        Ok(())
    }
}

/// `-1` when the hemisphere letter is the negative one, `1` otherwise.
/// (The `'S'`/`'s'` and `'W'`/`'w'` checks in the C parser.)
fn hemisphere_sign(item: &str, negative: u8) -> f32 {
    match item.as_bytes().first() {
        Some(&c) if c.eq_ignore_ascii_case(&negative) => -1.0,
        _ => 1.0,
    }
}

/// An empty field is `0`, as `strtof`/`strtol` returned for one in C; anything
/// non-numeric is an error instead of a silent `0`.
fn parse_f32(item: &str, statement: SentenceKind, index: u8) -> Result<f32, ParseError> {
    if item.is_empty() {
        return Ok(0.0);
    }
    item.parse::<f32>()
        .map_err(|_| ParseError::InvalidField { statement, index })
}

fn parse_u32(item: &str, statement: SentenceKind, index: u8) -> Result<u32, ParseError> {
    if item.is_empty() {
        return Ok(0);
    }
    item.parse::<u32>()
        .map_err(|_| ParseError::InvalidField { statement, index })
}

/// Port of `parse_lat_long()`: NMEA sends `ddmm.mmmm`, we want degrees.
fn parse_lat_long(item: &str, statement: SentenceKind, index: u8) -> Result<f32, ParseError> {
    let value = parse_f32(item, statement, index)?;
    let degrees = (value as i32) / 100;
    let minutes = value - (degrees as f32) * 100.0;
    Ok(degrees as f32 + minutes / 60.0)
}

/// Port of `parse_utc_time()`, with the shape of the field actually checked.
fn parse_utc_time(item: &str, previous: crate::GpsTime) -> Result<crate::GpsTime, ParseError> {
    if item.is_empty() {
        return Ok(previous);
    }
    let bytes = item.as_bytes();
    if bytes.len() < 6 || !bytes[..6].iter().all(u8::is_ascii_digit) {
        return Err(ParseError::InvalidTime);
    }
    let mut time = previous;
    time.hour = two_digits(&bytes[0..2]);
    time.minute = two_digits(&bytes[2..4]);
    time.second = two_digits(&bytes[4..6]);
    if time.hour > 23 || time.minute > 59 || time.second > 60 {
        return Err(ParseError::InvalidTime);
    }
    match bytes.get(6) {
        None => {}
        Some(b'.') => {
            let mut thousand: u16 = 0;
            for &digit in &bytes[7..] {
                if !digit.is_ascii_digit() {
                    return Err(ParseError::InvalidTime);
                }
                thousand = thousand
                    .wrapping_mul(10)
                    .wrapping_add(u16::from(digit - b'0'));
            }
            time.thousand = thousand;
        }
        Some(_) => return Err(ParseError::InvalidTime),
    }
    Ok(time)
}

/// Port of the `ddmmyy` handling in `parse_rmc()`. `year` stays two-digit, as
/// the C code stored it.
fn parse_date(item: &str, previous: crate::GpsDate) -> Result<crate::GpsDate, ParseError> {
    if item.is_empty() {
        return Ok(previous);
    }
    let bytes = item.as_bytes();
    if bytes.len() != 6 || !bytes.iter().all(u8::is_ascii_digit) {
        return Err(ParseError::InvalidDate);
    }
    let date = crate::GpsDate {
        day: two_digits(&bytes[0..2]),
        month: two_digits(&bytes[2..4]),
        year: u16::from(two_digits(&bytes[4..6])),
    };
    if date.day == 0 || date.day > 31 || date.month == 0 || date.month > 12 {
        return Err(ParseError::InvalidDate);
    }
    Ok(date)
}

/// Port of `convert_two_digit2number()`. Callers check the digits first.
fn two_digits(digits: &[u8]) -> u8 {
    10 * (digits[0] - b'0') + (digits[1] - b'0')
}

/// The two hex digits after `*`. The C code used `strtol(.., 16)`, which
/// happily returned `0` for a missing or garbage field.
fn parse_hex_u8(item: &str) -> Option<u8> {
    let bytes = item.as_bytes();
    if bytes.len() != 2 {
        return None;
    }
    let high = (bytes[0] as char).to_digit(16)?;
    let low = (bytes[1] as char).to_digit(16)?;
    Some((high * 16 + low) as u8)
}
