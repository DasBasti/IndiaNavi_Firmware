//! Port of `lib/Platinenmacher/parser/config.{c,h}` — the IndiaNavi config
//! parser for the XML-ish config data.
//!
//! **The C implementation is an empty stub.** `config_parser(const char
//! *xml_data)` includes `sxml.h`, defines an unused `COUNT()` macro and has an
//! empty body; nothing in `src/` calls it yet. The port keeps that contract
//! rather than inventing a parser: same signature, same no-op, so call sites
//! can be ported now and the real implementation lands in one place later.
//!
//! When the parser is actually written it should return
//! `Result<Config, Error>` instead of `()` — see `rust/PORTING.md` on
//! fallible APIs. Doing that now would be a new feature, not a port.

/// Parse the XML-ish configuration payload.
///
/// Does nothing, exactly like the C stub it replaces, and accepts any input
/// without failing.
pub fn config_parser(_xml_data: &str) {
    // Intentionally empty: mirrors the empty body of config.c.
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_parser_is_a_no_op_for_any_input() {
        // Guards the contract: whatever we feed it, it neither panics nor
        // reports anything, matching the C stub.
        config_parser("");
        config_parser("<config><wifi ssid=\"x\"/></config>");
        config_parser("not xml at all \u{0}\u{1}");
    }
}
