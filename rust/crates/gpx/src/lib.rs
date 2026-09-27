//! Streaming GPX parser.
//!
//! Port of `lib/Platinenmacher/parser/gpx.{c,h}`. The C version leaned on the
//! `lib/sxml` submodule and needed the whole `track.gpx` in one RAM buffer; this
//! one is fed arbitrary chunks straight from the SD card and keeps only a
//! handful of fixed-size scratch buffers, so it is `no_std` and allocation free.
//!
//! Track points (`trk`/`trkseg`/`trkpt`) and standalone waypoints (`wpt`) are
//! reported to a sink callback as they are completed, mirroring the
//! `add_waypoint_cb` of `gpx_parser()`.
//!
//! ```
//! # use gpx::{parse, PointKind};
//! let doc = br#"<gpx><trk><name>Ride</name><trkseg>
//!               <trkpt lat="49.622274" lon="8.587822"><ele>96.0</ele></trkpt>
//!               </trkseg></trk></gpx>"#;
//! let mut points = Vec::new();
//! let gpx = parse(doc, |wp| points.push(wp)).unwrap();
//! assert_eq!(gpx.track_name(), Some("Ride"));
//! assert_eq!(gpx.waypoints_num, 1);
//! assert_eq!(points[0].kind, PointKind::TrackPoint);
//! ```

#![cfg_attr(not(test), no_std)]

mod buf;
mod xml;

use buf::Buf;
use xml::{Event, Lexer, MAX_TEXT};

/// Why parsing stopped.
///
/// Mirrors the only failure `gpx.c` acted on, `SXML_ERROR_XMLINVALID`. A
/// document that merely stops early is *not* an error: as in the C version the
/// caller keeps whatever was complete, which is how a half-written track from a
/// yanked SD card still draws.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The document is not well-formed XML. Parsing stops here and the
    /// [`Parser`] stays failed; no result is produced.
    Malformed,
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Error::Malformed => f.write_str("malformed GPX document"),
        }
    }
}

/// Which GPX element a point came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PointKind {
    /// A `trkpt` inside a `trkseg`.
    TrackPoint,
    /// A standalone `wpt`.
    Waypoint,
}

/// One parsed point, the subset of `waypoint_t` that `gpx.c` filled in.
///
/// The remaining `waypoint_t` fields (screen position, colour, render hooks)
/// belong to the GUI and are left to the consumer of these records.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Waypoint {
    /// Latitude in degrees, from the `lat` attribute.
    pub lat: f32,
    /// Longitude in degrees, from the `lon` attribute.
    pub lon: f32,
    /// Elevation in metres, from the `ele` child element; `0.0` when absent.
    pub ele: f32,
    /// Whether this came from a `trkpt` or a `wpt`.
    pub kind: PointKind,
    /// 1-based position in the document, the count `add_waypoint_cb` returned.
    pub num: u32,
}

/// Summary of a parsed document, the Rust form of `gpx_t`.
#[derive(Clone, Copy)]
pub struct Gpx {
    name: Buf<MAX_TEXT>,
    has_name: bool,
    /// How many points were reported to the sink.
    pub waypoints_num: u32,
}

impl Gpx {
    /// The `trk`/`name` text, if the document carried one.
    ///
    /// Names longer than 64 bytes are truncated; a name that is not valid UTF-8
    /// is reported as `None`.
    pub fn track_name(&self) -> Option<&str> {
        if self.has_name {
            self.name.trimmed_str()
        } else {
            None
        }
    }
}

impl core::fmt::Debug for Gpx {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Gpx")
            .field("track_name", &self.track_name())
            .field("waypoints_num", &self.waypoints_num)
            .finish()
    }
}

impl PartialEq for Gpx {
    fn eq(&self, other: &Self) -> bool {
        self.track_name() == other.track_name() && self.waypoints_num == other.waypoints_num
    }
}

/// Where in the GPX element tree we are.
///
/// Same states as the `gpx_state_e` of `gpx.c`, plus the two `wpt` states. Like
/// the C version this tracks only the elements it cares about and ignores
/// everything else, rather than validating the whole document structure.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    Skip,
    Trk,
    TrkName,
    TrkSeg,
    TrkPt,
    TrkPtEle,
    Wpt,
    WptEle,
}

/// Incremental GPX parser.
///
/// Feed it chunks with [`Parser::feed`] in any sizes — splits mid-tag and
/// mid-attribute are fine — then ask for the summary with [`Parser::finish`].
/// Completed points go to `sink` as they are parsed.
pub struct Parser<F> {
    sink: F,
    lexer: Lexer,
    state: State,
    /// The point being built, complete once its end tag arrives.
    pending: Option<Waypoint>,
    /// `lat`/`lon` seen on the start tag currently being read.
    attr_lat: Option<f32>,
    attr_lon: Option<f32>,
    name: Buf<MAX_TEXT>,
    has_name: bool,
    count: u32,
    failed: bool,
}

impl<F: FnMut(Waypoint)> Parser<F> {
    /// Creates a parser that hands every completed point to `sink`.
    pub fn new(sink: F) -> Self {
        Self {
            sink,
            lexer: Lexer::new(),
            state: State::Skip,
            pending: None,
            attr_lat: None,
            attr_lon: None,
            name: Buf::new(),
            has_name: false,
            count: 0,
            failed: false,
        }
    }

    /// Feeds the next chunk of the document.
    ///
    /// Chunk boundaries may fall anywhere. Once this returns
    /// [`Error::Malformed`] the parser is spent and every later call fails the
    /// same way, matching how `gpx.c` left the parse loop on
    /// `SXML_ERROR_XMLINVALID`.
    pub fn feed(&mut self, chunk: &[u8]) -> Result<(), Error> {
        if self.failed {
            return Err(Error::Malformed);
        }
        for &byte in chunk {
            match self.lexer.push(byte) {
                Ok(None) => {}
                Ok(Some(Event::Start)) => self.on_start(),
                Ok(Some(Event::Empty)) => {
                    self.on_start();
                    self.on_end();
                }
                Ok(Some(Event::End)) => self.on_end(),
                Ok(Some(Event::Attribute)) => self.on_attribute(),
                Err(err) => {
                    self.failed = true;
                    return Err(err);
                }
            }
        }
        Ok(())
    }

    /// Returns the document summary, or [`Error::Malformed`] if the document was
    /// not well-formed.
    ///
    /// A document that simply ran out mid-element yields the points that were
    /// complete, exactly as `gpx_parser()` did on `SXML_ERROR_BUFFERDRY`.
    pub fn finish(&self) -> Result<Gpx, Error> {
        if self.failed {
            return Err(Error::Malformed);
        }
        Ok(Gpx {
            name: self.name,
            has_name: self.has_name,
            waypoints_num: self.count,
        })
    }

    /// A start tag is complete; `lat`/`lon` for it have already been collected.
    fn on_start(&mut self) {
        let name = *self.lexer.name();
        let lat = self.attr_lat.take().unwrap_or(0.0);
        let lon = self.attr_lon.take().unwrap_or(0.0);
        match self.state {
            State::Skip if name.matches("trk") => self.state = State::Trk,
            State::Skip if name.matches("wpt") => {
                self.begin_point(lat, lon, PointKind::Waypoint);
                self.state = State::Wpt;
            }
            State::Trk if name.matches("name") => {
                self.state = State::TrkName;
                self.lexer.capture_text();
            }
            State::Trk if name.matches("trkseg") => self.state = State::TrkSeg,
            State::TrkSeg if name.matches("trkpt") => {
                self.begin_point(lat, lon, PointKind::TrackPoint);
                self.state = State::TrkPt;
            }
            State::TrkPt if name.matches("ele") => {
                self.state = State::TrkPtEle;
                self.lexer.capture_text();
            }
            State::Wpt if name.matches("ele") => {
                self.state = State::WptEle;
                self.lexer.capture_text();
            }
            _ => {}
        }
    }

    /// An end tag is complete.
    fn on_end(&mut self) {
        let name = *self.lexer.name();
        match self.state {
            State::Trk if name.matches("trk") => self.state = State::Skip,
            State::TrkName if name.matches("name") => {
                self.take_track_name();
                self.state = State::Trk;
            }
            State::TrkSeg if name.matches("trkseg") => self.state = State::Trk,
            State::TrkPt if name.matches("trkpt") => {
                self.emit_point();
                self.state = State::TrkSeg;
            }
            State::TrkPtEle if name.matches("ele") => {
                self.take_ele();
                self.state = State::TrkPt;
            }
            State::Wpt if name.matches("wpt") => {
                self.emit_point();
                self.state = State::Skip;
            }
            State::WptEle if name.matches("ele") => {
                self.take_ele();
                self.state = State::Wpt;
            }
            _ => {}
        }
    }

    /// An attribute is complete. Only `lat`/`lon` on a point element matter.
    fn on_attribute(&mut self) {
        let tag = *self.lexer.name();
        if !tag.matches("trkpt") && !tag.matches("wpt") {
            return;
        }
        let attr = *self.lexer.attr_name();
        let value = *self.lexer.attr_value();
        if attr.matches("lat") {
            self.attr_lat = Some(parse_f32(&value));
        } else if attr.matches("lon") {
            self.attr_lon = Some(parse_f32(&value));
        }
    }

    fn begin_point(&mut self, lat: f32, lon: f32, kind: PointKind) {
        self.pending = Some(Waypoint {
            lat,
            lon,
            ele: 0.0,
            kind,
            num: 0,
        });
    }

    fn emit_point(&mut self) {
        if let Some(mut wp) = self.pending.take() {
            self.count += 1;
            wp.num = self.count;
            (self.sink)(wp);
        }
    }

    fn take_ele(&mut self) {
        let text = *self.lexer.text();
        self.lexer.stop_capture_text();
        if let Some(wp) = self.pending.as_mut() {
            wp.ele = parse_f32(&text);
        }
    }

    fn take_track_name(&mut self) {
        let text = *self.lexer.text();
        self.lexer.stop_capture_text();
        if let Some(name) = text.trimmed_str() {
            self.name.clear();
            for &byte in name.as_bytes() {
                self.name.push(byte);
            }
            self.has_name = true;
        }
    }
}

/// Parses a number the way `gpx.c` did: `atoff()` yields `0.0` for anything it
/// cannot read, and so do we.
fn parse_f32<const N: usize>(buf: &Buf<N>) -> f32 {
    buf.trimmed_str()
        .and_then(|s| s.parse::<f32>().ok())
        .unwrap_or(0.0)
}

/// Parses a whole document in one go, the equivalent of `gpx_parser()`.
pub fn parse<F: FnMut(Waypoint)>(data: &[u8], sink: F) -> Result<Gpx, Error> {
    let mut parser = Parser::new(sink);
    parser.feed(data)?;
    parser.finish()
}
