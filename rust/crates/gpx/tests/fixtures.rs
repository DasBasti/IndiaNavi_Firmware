//! The fixtures and assertions of `test/host/Platinenmacher/test_gpx/test.c`,
//! carried over to the Rust parser.
//!
//! `test.c` drove `gpx_parser()` with a `modify_waypoint` callback that counted
//! the points and flagged each one `active`; here the sink collects them, so
//! "the callback saw every point" is `points.len()`.

use gpx::{parse, Error, PointKind, Waypoint};

fn fixture(name: &str) -> Vec<u8> {
    let path = format!(
        "{}/../../../test/host/Platinenmacher/test_gpx/{name}",
        env!("CARGO_MANIFEST_DIR")
    );
    std::fs::read(&path).unwrap_or_else(|e| panic!("cannot read fixture {path}: {e}"))
}

/// `test_gpx_parsing`: a complete document.
#[test]
fn parses_valid_document() {
    let mut points = Vec::new();
    let gpx = parse(&fixture("test.gpx"), |wp| points.push(wp)).expect("valid document");

    assert_eq!(gpx.track_name(), Some("Teststrecke"));
    assert_eq!(gpx.waypoints_num, 17);
    assert_eq!(points.len(), 17);

    let start = points[0];
    assert_eq!(start.lat, 49.622274);
    assert_eq!(start.lon, 8.587822);
    assert_eq!(start.ele, 96.014564);
    assert_eq!(start.kind, PointKind::TrackPoint);

    // `add_waypoint_cb` returned the running count; `num` carries it now.
    let nums: Vec<u32> = points.iter().map(|wp| wp.num).collect();
    assert_eq!(nums, (1..=17).collect::<Vec<u32>>());

    // Every point in this fixture carries all three values.
    assert!(points
        .iter()
        .all(|wp| wp.lat > 49.0 && wp.lon > 8.0 && wp.ele > 90.0));
}

/// `test_gpx_parsing_incomplete`: the document is cut off mid-`trkpt`. The two
/// points that were complete survive, as they did in C.
#[test]
fn parses_truncated_document_up_to_the_cut() {
    let mut points = Vec::new();
    let gpx = parse(&fixture("test_incomplete.gpx"), |wp| points.push(wp))
        .expect("a truncated document is not an error");

    assert_eq!(gpx.track_name(), Some("Teststrecke"));
    assert_eq!(gpx.waypoints_num, points.len() as u32);
    assert_eq!(gpx.waypoints_num, 2);

    let start = points[0];
    assert_eq!(start.lat, 49.622274);
    assert_eq!(start.lon, 8.587822);
}

/// `test_gpx_parsing_error`: `<trkseg` is never closed, so the document is
/// malformed. C reported no track name and no waypoints; here the parse fails
/// and there is no result at all.
#[test]
fn rejects_malformed_document() {
    let mut points = Vec::new();
    let result = parse(&fixture("test_error.gpx"), |wp| points.push(wp));

    assert_eq!(result, Err(Error::Malformed));
    assert!(
        points.is_empty(),
        "no waypoints reported before the error: {points:?}"
    );
}

/// The error is sticky: once the document is bad, feeding the rest of it does
/// not resurrect the parse.
#[test]
fn malformed_document_stays_failed() {
    let mut parser = gpx::Parser::new(|_: Waypoint| {});
    assert_eq!(
        parser.feed(&fixture("test_error.gpx")),
        Err(Error::Malformed)
    );
    assert_eq!(
        parser.feed(b"<trkpt lat=\"1\" lon=\"2\"/>"),
        Err(Error::Malformed)
    );
    assert_eq!(parser.finish(), Err(Error::Malformed));
}
