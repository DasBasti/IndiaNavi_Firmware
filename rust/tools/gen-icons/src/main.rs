//! gen-icons — regenerates `rust/crates/pm-icons/src/icons_32.rs` from the C
//! image assets in `lib/icons_32/`.
//!
//! The icon bytes stay auditable: they are transcribed verbatim from the C
//! arrays, and this tool is the only thing allowed to write the generated
//! module. Run it after touching `lib/icons_32/`:
//!
//! ```text
//! cargo run --manifest-path rust/tools/gen-icons/Cargo.toml
//! cargo run --manifest-path rust/tools/gen-icons/Cargo.toml -- --check
//! ```
//!
//! `--check` regenerates in memory and fails if the committed file differs, so
//! CI can prove the generated source is reproducible.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

/// Bits per pixel of the icon data.
///
/// The assets are 3-bit ACeP colour codes packed two per byte, high nibble
/// first — see `ACEP_5IN65_Decompress_Pixel` in
/// `lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.c`.
const BITS_PER_PIXEL: usize = 4;

/// Bytes per emitted line; one 32px display row is exactly 16 bytes.
const BYTES_PER_LINE: usize = 16;

struct Icon {
    /// C symbol name, e.g. `GPS_lock`.
    c_name: String,
    /// Rust const name, e.g. `GPS_LOCK`.
    rust_name: String,
    /// File the symbol is defined in, e.g. `GPS.png.c`.
    source: String,
    data: Vec<u8>,
}

fn main() -> ExitCode {
    let check = match parse_args() {
        Ok(check) => check,
        Err(msg) => {
            eprintln!("gen-icons: {msg}");
            eprintln!("usage: gen-icons [--check]");
            return ExitCode::FAILURE;
        }
    };

    let repo_root = repo_root();
    let icons_dir = repo_root.join("lib/icons_32");
    let out_path = repo_root.join("rust/crates/pm-icons/src/icons_32.rs");

    let generated = match generate(&icons_dir) {
        Ok(generated) => generated,
        Err(msg) => {
            eprintln!("gen-icons: {msg}");
            return ExitCode::FAILURE;
        }
    };

    if check {
        let current = std::fs::read_to_string(&out_path).unwrap_or_default();
        if current == generated {
            println!("gen-icons: {} is up to date", out_path.display());
            return ExitCode::SUCCESS;
        }
        eprintln!(
            "gen-icons: {} is stale, re-run gen-icons without --check",
            out_path.display()
        );
        return ExitCode::FAILURE;
    }

    if let Err(e) = std::fs::write(&out_path, &generated) {
        eprintln!("gen-icons: cannot write {}: {e}", out_path.display());
        return ExitCode::FAILURE;
    }
    println!("gen-icons: wrote {}", out_path.display());
    ExitCode::SUCCESS
}

fn parse_args() -> Result<bool, String> {
    let mut check = false;
    for arg in std::env::args().skip(1) {
        match arg.as_str() {
            "--check" => check = true,
            other => return Err(format!("unknown argument {other:?}")),
        }
    }
    Ok(check)
}

/// Repo root, derived from this package's location so the tool works from any
/// working directory: `<root>/rust/tools/gen-icons` -> `<root>`.
fn repo_root() -> PathBuf {
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    manifest_dir
        .ancestors()
        .nth(3)
        .expect("gen-icons lives at <repo>/rust/tools/gen-icons")
        .to_path_buf()
}

fn generate(icons_dir: &Path) -> Result<String, String> {
    let header = read(&icons_dir.join("icons_32.h"))?;
    let icon_size = parse_icon_size(&header)?;
    let externs = parse_externs(&header);
    if externs.is_empty() {
        return Err("no `extern uint8_t <name>[];` declarations in icons_32.h".into());
    }

    let mut icons = Vec::new();
    for entry in c_sources(icons_dir)? {
        let text = read(&entry)?;
        let file_name = file_name(&entry);
        let (c_name, data) = parse_array(&text, &file_name)?;
        icons.push(Icon {
            rust_name: rust_name(&c_name),
            c_name,
            source: file_name,
            data,
        });
    }

    // Every declared icon must be defined exactly once, and every definition
    // must be declared — a mismatch means an asset was added or removed
    // without updating icons_32.h.
    for name in &externs {
        let defs = icons.iter().filter(|i| &i.c_name == name).count();
        if defs != 1 {
            return Err(format!(
                "icons_32.h declares `{name}` but {defs} .c files define it"
            ));
        }
    }
    for icon in &icons {
        if !externs.contains(&icon.c_name) {
            return Err(format!(
                "{} defines `{}` which icons_32.h does not declare",
                icon.source, icon.c_name
            ));
        }
    }

    let expected_len = icon_size * icon_size * BITS_PER_PIXEL / 8;
    for icon in &icons {
        if icon.data.len() != expected_len {
            return Err(format!(
                "{}: `{}` has {} bytes, expected {} for {icon_size}x{icon_size} at {BITS_PER_PIXEL}bpp",
                icon.source,
                icon.c_name,
                icon.data.len(),
                expected_len
            ));
        }
        // Both nibbles are 3-bit colour codes; anything above 7 is not a
        // colour this display can show and means the asset is not in the
        // format we assume.
        if let Some(bad) = icon.data.iter().find(|b| *b & 0x88 != 0) {
            return Err(format!(
                "{}: `{}` contains byte {bad:#04x} with a nibble > 7",
                icon.source, icon.c_name
            ));
        }
    }

    // Emit in icons_32.h declaration order so the generated module can be read
    // side by side with the C header.
    let mut ordered = Vec::with_capacity(externs.len());
    for name in &externs {
        let icon = icons
            .iter()
            .find(|i| &i.c_name == name)
            .expect("checked above");
        ordered.push(icon);
    }

    Ok(render(&ordered, icon_size))
}

fn render(icons: &[&Icon], icon_size: usize) -> String {
    let mut out = String::new();
    out.push_str(concat!(
        "// @generated by rust/tools/gen-icons from lib/icons_32/ — do not edit by hand.\n",
        "//\n",
        "// Replaces the icon arrays of lib/icons_32/*.c. Regenerate with:\n",
        "//     cargo run --manifest-path rust/tools/gen-icons/Cargo.toml\n",
        "\n",
        "// One line per 32px display row, like the C sources, so leave it unformatted.\n",
        "#![cfg_attr(rustfmt, rustfmt::skip)]\n",
        "\n",
        "use crate::Icon;\n",
    ));

    for icon in icons {
        let _ = write!(
            out,
            "\n/// `{}` — replaces `lib/icons_32/{}`.\npub const {}: Icon = Icon::new({icon_size}, {icon_size}, &[\n",
            icon.c_name, icon.source, icon.rust_name
        );
        for chunk in icon.data.chunks(BYTES_PER_LINE) {
            out.push_str("   ");
            for byte in chunk {
                let _ = write!(out, " {byte:#04x},");
            }
            out.push('\n');
        }
        out.push_str("]);\n");
    }

    out.push_str(concat!(
        "\n/// Every icon in this module paired with its C symbol name, in the order the\n",
        "/// externs are declared in `lib/icons_32/icons_32.h`.\n",
        "pub const ALL: &[(&str, &Icon)] = &[\n",
    ));
    for icon in icons {
        let _ = writeln!(out, "    (\"{}\", &{}),", icon.c_name, icon.rust_name);
    }
    out.push_str("];\n");
    out
}

fn c_sources(dir: &Path) -> Result<Vec<PathBuf>, String> {
    let mut files = Vec::new();
    let entries =
        std::fs::read_dir(dir).map_err(|e| format!("cannot read {}: {e}", dir.display()))?;
    for entry in entries {
        let path = entry
            .map_err(|e| format!("cannot read {}: {e}", dir.display()))?
            .path();
        if path.extension().is_some_and(|ext| ext == "c") {
            files.push(path);
        }
    }
    // Stable order regardless of the filesystem's directory order.
    files.sort();
    Ok(files)
}

fn parse_icon_size(header: &str) -> Result<usize, String> {
    for line in header.lines() {
        let Some(rest) = line.trim().strip_prefix("#define ICON_SIZE") else {
            continue;
        };
        return rest
            .trim()
            .parse()
            .map_err(|e| format!("icons_32.h: cannot parse ICON_SIZE: {e}"));
    }
    Err("icons_32.h: no `#define ICON_SIZE`".into())
}

/// Collects `extern uint8_t <name>[];` symbol names in declaration order.
fn parse_externs(header: &str) -> Vec<String> {
    let mut names = Vec::new();
    for line in header.lines() {
        let line = line.trim();
        let Some(rest) = line.strip_prefix("extern ") else {
            continue;
        };
        let Some(rest) = rest.strip_suffix("[];") else {
            continue;
        };
        // `uint8_t <name>` — the last whitespace-separated word is the symbol.
        if let Some(name) = rest.split_whitespace().next_back() {
            names.push(name.to_string());
        }
    }
    names
}

/// Parses the single `uint8_t <name>[] = { 0x.., ... };` array of a C asset
/// file, returning its symbol name and bytes.
fn parse_array(text: &str, file_name: &str) -> Result<(String, Vec<u8>), String> {
    let open = text
        .find('{')
        .ok_or_else(|| format!("{file_name}: no array initialiser"))?;
    let close = text
        .rfind('}')
        .ok_or_else(|| format!("{file_name}: unterminated array initialiser"))?;
    if close < open {
        return Err(format!("{file_name}: malformed array initialiser"));
    }

    let decl = text[..open].trim_end();
    let decl = decl
        .trim_end()
        .strip_suffix('=')
        .unwrap_or(decl)
        .trim_end()
        .strip_suffix("[]")
        .ok_or_else(|| format!("{file_name}: cannot find `<name>[]` before the initialiser"))?;
    let c_name = decl
        .split(|c: char| c.is_whitespace() || c == '*')
        .next_back()
        .filter(|name| !name.is_empty())
        .ok_or_else(|| format!("{file_name}: cannot read the array symbol name"))?
        .to_string();

    let mut data = Vec::new();
    for token in text[open + 1..close].split(',') {
        let token = token.trim();
        if token.is_empty() {
            continue;
        }
        let hex = token
            .strip_prefix("0x")
            .or_else(|| token.strip_prefix("0X"))
            .ok_or_else(|| format!("{file_name}: `{token}` is not a 0x.. byte literal"))?;
        let byte = u8::from_str_radix(hex, 16)
            .map_err(|e| format!("{file_name}: cannot parse `{token}`: {e}"))?;
        data.push(byte);
    }
    if data.is_empty() {
        return Err(format!("{file_name}: array `{c_name}` is empty"));
    }
    Ok((c_name, data))
}

/// C symbol -> Rust const name: split lower->upper transitions, then upcase.
/// `noGPS` -> `NO_GPS`, `GPS_lock` -> `GPS_LOCK`, `bat_100` -> `BAT_100`.
fn rust_name(c_name: &str) -> String {
    let mut out = String::with_capacity(c_name.len() + 1);
    let mut prev: Option<char> = None;
    for c in c_name.chars() {
        if c.is_ascii_uppercase() && prev.is_some_and(|p| p.is_ascii_lowercase()) {
            out.push('_');
        }
        out.push(c.to_ascii_uppercase());
        prev = Some(c);
    }
    out
}

fn read(path: &Path) -> Result<String, String> {
    std::fs::read_to_string(path).map_err(|e| format!("cannot read {}: {e}", path.display()))
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .expect("directory entries have file names")
        .to_string_lossy()
        .into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rust_names_follow_the_documented_rule() {
        assert_eq!(rust_name("GPS"), "GPS");
        assert_eq!(rust_name("GPS_lock"), "GPS_LOCK");
        assert_eq!(rust_name("noGPS"), "NO_GPS");
        assert_eq!(rust_name("noSD"), "NO_SD");
        assert_eq!(rust_name("bat_100"), "BAT_100");
        assert_eq!(rust_name("WIFI_0"), "WIFI_0");
        assert_eq!(rust_name("path"), "PATH");
    }

    #[test]
    fn parses_a_const_qualified_array() {
        let (name, data) = parse_array(
            "#include <stdint.h>\nconst uint8_t bat_0[]={\n0x77, 0x00,\n};\n",
            "x.c",
        )
        .unwrap();
        assert_eq!(name, "bat_0");
        assert_eq!(data, vec![0x77, 0x00]);
    }

    #[test]
    fn parses_a_spaced_array_declaration() {
        let (name, data) =
            parse_array("uint8_t norden[] = {\n    0x07,\n    0x70,\n};\n", "x.c").unwrap();
        assert_eq!(name, "norden");
        assert_eq!(data, vec![0x07, 0x70]);
    }

    #[test]
    fn rejects_non_hex_bytes() {
        assert!(parse_array("uint8_t a[] = { 12 };", "x.c").is_err());
    }

    #[test]
    fn reads_icon_size_and_externs_from_the_header() {
        let header = "extern uint8_t bat_100[];\nextern uint8_t GPS[];\n#define ICON_SIZE 32\n";
        assert_eq!(parse_icon_size(header).unwrap(), 32);
        assert_eq!(parse_externs(header), vec!["bat_100", "GPS"]);
    }
}
