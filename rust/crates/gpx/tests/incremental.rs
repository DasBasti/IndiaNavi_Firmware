//! The firmware reads `track.gpx` off the SD card in chunks, so a chunked parse
//! has to give the same answer as a single-shot one no matter where the chunk
//! boundaries land.

use gpx::{parse, Gpx, Parser, Waypoint};

fn single_shot(doc: &[u8]) -> (Gpx, Vec<Waypoint>) {
    let mut points = Vec::new();
    let gpx = parse(doc, |wp| points.push(wp)).expect("valid document");
    (gpx, points)
}

fn chunked(chunks: &[&[u8]]) -> (Gpx, Vec<Waypoint>) {
    let mut points = Vec::new();
    let mut parser = Parser::new(|wp| points.push(wp));
    for chunk in chunks {
        parser.feed(chunk).expect("valid document");
    }
    let gpx = parser.finish().expect("valid document");
    (gpx, points)
}

fn in_chunks_of(doc: &[u8], size: usize) -> (Gpx, Vec<Waypoint>) {
    let chunks: Vec<&[u8]> = doc.chunks(size).collect();
    chunked(&chunks)
}

fn fixture(name: &str) -> Vec<u8> {
    let path = format!(
        "{}/../../../test/host/Platinenmacher/test_gpx/{name}",
        env!("CARGO_MANIFEST_DIR")
    );
    std::fs::read(path).unwrap()
}

/// Every chunk size, including one byte at a time, has to agree with the
/// single-shot parse. Size 1 guarantees splits inside tags, attribute names,
/// attribute values and character data.
#[test]
fn every_chunk_size_matches_single_shot() {
    for name in ["test.gpx", "test_incomplete.gpx"] {
        let doc = fixture(name);
        let expected = single_shot(&doc);
        for size in [1, 2, 3, 5, 7, 13, 64, 512, 4096] {
            assert_eq!(
                in_chunks_of(&doc, size),
                expected,
                "{name} in chunks of {size}"
            );
        }
    }
}

/// The same, spelled out: boundaries inside an element name, an attribute name,
/// an attribute value and element text.
#[test]
fn boundaries_inside_tags_and_attributes() {
    let chunks: &[&[u8]] = &[
        b"<gpx><trk><na", // mid element name
        b"me>Test",       // mid text
        b"strecke</name><trkseg><trkp",
        b"t la",     // mid attribute name
        b"t=\"49.6", // mid attribute value
        b"22274\" lo",
        b"n=\"8.5",
        b"87822\"><ele>96.0",
        b"14564</ele></trkpt></trkseg></trk></gpx>",
    ];
    let doc: Vec<u8> = chunks.concat();

    assert_eq!(chunked(chunks), single_shot(&doc));

    let (gpx, points) = chunked(chunks);
    assert_eq!(gpx.track_name(), Some("Teststrecke"));
    assert_eq!(gpx.waypoints_num, 1);
    assert_eq!(points[0].lat, 49.622274);
    assert_eq!(points[0].lon, 8.587822);
    assert_eq!(points[0].ele, 96.014564);
}

/// A malformed document is rejected at the same point however it is chunked.
#[test]
fn malformed_document_fails_when_chunked() {
    let doc = fixture("test_error.gpx");
    for size in [1, 3, 64, 4096] {
        let mut points = Vec::new();
        let mut parser = Parser::new(|wp: Waypoint| points.push(wp));
        let failed = doc.chunks(size).any(|chunk| parser.feed(chunk).is_err());
        assert!(failed, "should fail in chunks of {size}");
        assert!(parser.finish().is_err());
        assert!(points.is_empty());
    }
}

/// Empty chunks are harmless — a short read is not the end of the document.
#[test]
fn empty_chunks_are_ignored() {
    let doc = fixture("test.gpx");
    let mut chunks: Vec<&[u8]> = Vec::new();
    for chunk in doc.chunks(100) {
        chunks.push(b"");
        chunks.push(chunk);
    }
    assert_eq!(chunked(&chunks), single_shot(&doc));
}
