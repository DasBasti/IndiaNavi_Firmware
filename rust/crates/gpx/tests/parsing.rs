//! Element-level behaviour of the parser: which elements it reads, which it
//! ignores, and what it treats as malformed.

use gpx::{parse, Error, PointKind, Waypoint};

fn run(doc: &[u8]) -> (Result<gpx::Gpx, Error>, Vec<Waypoint>) {
    let mut points = Vec::new();
    let result = parse(doc, |wp| points.push(wp));
    (result, points)
}

fn points_of(doc: &[u8]) -> Vec<Waypoint> {
    let (result, points) = run(doc);
    result.expect("valid document");
    points
}

/// Standalone `wpt` elements are parsed too — `gpx.c` only ever looked at
/// `trkpt`.
#[test]
fn parses_standalone_waypoints() {
    let points = points_of(
        br#"<gpx>
              <wpt lat="49.1" lon="8.2"><name>Start</name><ele>101.5</ele></wpt>
              <wpt lat="49.3" lon="8.4"><ele>102.5</ele></wpt>
            </gpx>"#,
    );

    assert_eq!(points.len(), 2);
    assert_eq!(points[0].kind, PointKind::Waypoint);
    assert_eq!(
        (points[0].lat, points[0].lon, points[0].ele),
        (49.1, 8.2, 101.5)
    );
    assert_eq!(
        (points[1].lat, points[1].lon, points[1].ele),
        (49.3, 8.4, 102.5)
    );
    assert_eq!(points[1].num, 2);
}

/// Track points and waypoints in one document keep their own kind and share the
/// numbering.
#[test]
fn parses_track_points_and_waypoints_together() {
    let points = points_of(
        br#"<gpx>
              <wpt lat="1.0" lon="2.0"/>
              <trk><trkseg><trkpt lat="3.0" lon="4.0"/></trkseg></trk>
            </gpx>"#,
    );

    assert_eq!(points.len(), 2);
    assert_eq!(points[0].kind, PointKind::Waypoint);
    assert_eq!(points[1].kind, PointKind::TrackPoint);
    assert_eq!(points[1].num, 2);
}

/// Self-closing point elements are complete points.
#[test]
fn handles_self_closing_points() {
    let points =
        points_of(br#"<gpx><trk><trkseg><trkpt lat="49.0" lon="8.0" /></trkseg></trk></gpx>"#);

    assert_eq!(points.len(), 1);
    assert_eq!((points[0].lat, points[0].lon), (49.0, 8.0));
    // No `ele` child, so no elevation.
    assert_eq!(points[0].ele, 0.0);
}

/// Single-quoted attribute values are valid XML.
#[test]
fn accepts_single_quoted_attributes() {
    let points =
        points_of(b"<gpx><trk><trkseg><trkpt lat='49.5' lon='8.5'></trkpt></trkseg></trk></gpx>");
    assert_eq!((points[0].lat, points[0].lon), (49.5, 8.5));
}

/// Negative and exponent-free decimal coordinates round-trip.
#[test]
fn parses_negative_coordinates() {
    let points =
        points_of(br#"<gpx><wpt lat="-33.8688" lon="-151.2093"><ele>-12.5</ele></wpt></gpx>"#);
    assert_eq!(
        (points[0].lat, points[0].lon, points[0].ele),
        (-33.8688, -151.2093, -12.5)
    );
}

/// Unparsable numbers become `0.0`, which is what `atoff()` gave C.
#[test]
fn unparsable_numbers_become_zero() {
    let points = points_of(br#"<gpx><wpt lat="north" lon=""><ele>high</ele></wpt></gpx>"#);
    assert_eq!(
        (points[0].lat, points[0].lon, points[0].ele),
        (0.0, 0.0, 0.0)
    );
}

/// A point element with no `lat`/`lon` at all still reports, at the origin —
/// the attributes of an earlier point must not leak into it.
#[test]
fn missing_coordinates_do_not_leak_from_the_previous_point() {
    let points = points_of(br#"<gpx><wpt lat="49.0" lon="8.0"/><wpt/><wpt lon="8.1"/></gpx>"#);

    assert_eq!(points.len(), 3);
    assert_eq!((points[1].lat, points[1].lon), (0.0, 0.0));
    assert_eq!((points[2].lat, points[2].lon), (0.0, 8.1));
}

/// The track name comes from `trk`/`name`, not from `metadata`/`name`.
#[test]
fn track_name_comes_from_the_track_not_the_metadata() {
    let (result, _) = run(br#"<gpx>
              <metadata><name><![CDATA[Metadata name]]></name></metadata>
              <trk><name>Track name</name></trk>
            </gpx>"#);

    assert_eq!(result.unwrap().track_name(), Some("Track name"));
}

/// A CDATA-wrapped track name is read as text, and surrounding whitespace is
/// trimmed.
#[test]
fn reads_cdata_and_whitespace_padded_names() {
    let (result, _) = run(b"<gpx><trk><name>\n  <![CDATA[Ride 7]]>\n  </name></trk></gpx>");
    assert_eq!(result.unwrap().track_name(), Some("Ride 7"));
}

/// No `trk`/`name` means no track name.
#[test]
fn documents_without_a_track_name() {
    let (result, points) =
        run(br#"<gpx><trk><trkseg><trkpt lat="1" lon="2"/></trkseg></trk></gpx>"#);
    let gpx = result.unwrap();
    assert_eq!(gpx.track_name(), None);
    assert_eq!(gpx.waypoints_num, 1);
    assert_eq!(points.len(), 1);
}

/// Prologue, doctype with an internal subset, comments and unknown elements are
/// all skipped; `test.gpx` contains each of them.
#[test]
fn skips_prologue_doctype_comments_and_unknown_elements() {
    let (result, points) = run(br#"<?xml version='1.0' encoding='UTF-8'?>
            <!DOCTYPE gpx[]>
            <!-- a comment with <tags> and -- dashes -->
            <gpx><metadata><link href="https://example.com"><text>x</text></link></metadata>
              <trk><name>T</name><trkseg>
                <trkpt lat="49.0" lon="8.0">
                  <ele>96.0</ele><time>2022-10-01T20:42:40.665Z</time>
                  <extensions><gpxtpx:hr>140</gpxtpx:hr></extensions>
                </trkpt>
              </trkseg></trk>
            </gpx>"#);

    let gpx = result.unwrap();
    assert_eq!(gpx.track_name(), Some("T"));
    assert_eq!(gpx.waypoints_num, 1);
    assert_eq!(
        (points[0].lat, points[0].lon, points[0].ele),
        (49.0, 8.0, 96.0)
    );
}

/// Several segments in one track all contribute points.
#[test]
fn parses_multiple_track_segments() {
    let points = points_of(
        br#"<gpx><trk>
              <trkseg><trkpt lat="1" lon="1"/><trkpt lat="2" lon="2"/></trkseg>
              <trkseg><trkpt lat="3" lon="3"/></trkseg>
            </trk></gpx>"#,
    );
    assert_eq!(points.len(), 3);
    assert_eq!(points[2].lat, 3.0);
}

/// Things that are not well-formed XML.
#[test]
fn malformed_documents_are_rejected() {
    let cases: &[(&str, &[u8])] = &[
        (
            "unclosed start tag",
            b"<gpx><trk><trkseg\n<trkpt lat=\"1\" lon=\"2\"/></trkseg></trk></gpx>",
        ),
        (
            "new tag opened before the previous one closed",
            b"<gpx><trkseg <x=\"1\"><trkpt lat=\"1\" lon=\"2\"/></trkseg></gpx>",
        ),
        (
            "attribute without value",
            br#"<gpx><trk><trkseg><trkpt lat lon="2"/></trkseg></trk></gpx>"#,
        ),
        (
            "unquoted attribute value",
            b"<gpx><trkpt lat=1></trkpt></gpx>",
        ),
        (
            "stray < in attribute value",
            b"<gpx><wpt lat=\"<1\"/></gpx>",
        ),
        ("empty tag name", b"<gpx>< trk></gpx>"),
        ("bad comment opener", b"<gpx><!-oops--></gpx>"),
        ("bad cdata opener", b"<gpx><![CDAT[x]]></gpx>"),
        ("junk after end tag name", b"<gpx><trk></trk x></gpx>"),
        ("slash not closing the tag", b"<gpx><wpt lat=\"1\"/x></gpx>"),
    ];

    for (what, doc) in cases {
        let (result, points) = run(doc);
        assert_eq!(result, Err(Error::Malformed), "should reject {what}");
        assert!(points.is_empty(), "no points from {what}");
    }
}

/// Truncation anywhere is tolerated: whatever was complete is kept. Cutting the
/// valid fixture at every byte offset must never panic and never fail.
#[test]
fn truncation_at_any_offset_is_tolerated() {
    let doc: &[u8] = br#"<gpx><trk><name>T</name><trkseg><trkpt lat="49.0" lon="8.0"><ele>96.0</ele></trkpt><trkpt lat="49.1" lon="8.1"/></trkseg></trk></gpx>"#;

    for cut in 0..doc.len() {
        let (result, points) = run(&doc[..cut]);
        let gpx = result.unwrap_or_else(|e| panic!("cut at {cut} should not fail: {e}"));
        assert_eq!(gpx.waypoints_num, points.len() as u32, "cut at {cut}");
        assert!(points.len() <= 2, "cut at {cut}");
    }

    // The full document still has both points.
    let (result, points) = run(doc);
    assert_eq!(result.unwrap().waypoints_num, 2);
    assert_eq!(points.len(), 2);
}

/// Over-long names and values must not overflow a buffer; they are simply not
/// recognised. A 64-byte track name is kept, a longer one is truncated.
#[test]
fn oversized_tokens_are_bounded() {
    let long_element = format!(
        "<gpx><{0} lat=\"1\" lon=\"2\"></{0}></gpx>",
        "x".repeat(200)
    );
    let (result, points) = run(long_element.as_bytes());
    assert_eq!(result.unwrap().waypoints_num, 0);
    assert!(points.is_empty());

    let name = "n".repeat(200);
    let (result, _) = run(format!("<gpx><trk><name>{name}</name></trk></gpx>").as_bytes());
    let gpx = result.unwrap();
    let track_name = gpx.track_name().expect("a truncated name is still a name");
    assert_eq!(track_name, "n".repeat(64));
}

/// An empty document parses to nothing.
#[test]
fn empty_document() {
    let (result, points) = run(b"");
    let gpx = result.unwrap();
    assert_eq!(gpx.track_name(), None);
    assert_eq!(gpx.waypoints_num, 0);
    assert!(points.is_empty());
}
