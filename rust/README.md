# Rust port

Cargo workspace for the Rust port of the `lib/Platinenmacher` C libraries.

| crate | ported from |
| --- | --- |
| `crates/pm-core` | `error.h`, and the framebuffer surface of `display.h` (`display_t`, `display_pixel_draw()`) |
| `crates/pm-font` | `font.[ch]`, the text drawing part of `display.c`, `lib/helper/umlaut.c` and `lib/Platinenmacher/fonts/` |

Both crates are `no_std`; `pm-core` additionally uses `alloc` for its
`MockDisplay` framebuffer. `cargo build --target thumbv7em-none-eabihf` builds
the workspace for bare metal.

```sh
cd rust
cargo test                           # run the host test suite
cargo test -p pm-font                # ... just the font crate
node tools/convert-fonts.mjs --check # verify the generated font tables are in sync
```

`tools/convert-fonts.mjs` regenerates `crates/pm-font/src/fonts/` from the C font
headers. The generated files are committed so the crate builds without node, but
they must never be edited by hand -- change the C table and re-run the converter.
The generator emits `cargo fmt` shaped output, so formatting the workspace never
makes the tables look stale.

The port is deliberately bug-for-bug compatible with the C code: glyph
placement, the column advance that ignores `font->width`, and the umlaut folding
table all reproduce what the firmware does today. Every such spot is marked with
a `C quirk:` comment.
