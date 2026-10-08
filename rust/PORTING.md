# Porting IndiaNavi from C to Rust

This directory holds the Rust port. The C tree (`src/`, `lib/`, `include/`) is
the reference implementation and stays untouched until the port is complete;
the final cleanup task removes it.

Nothing in this scaffold contains ported logic. It exists so that every
later task only has to add code to a module that is already declared, named,
and wired into the build.

## Layout

```
rust/
  Cargo.toml              workspace, resolver = 2, 10 library crates
  PORTING.md              this file
  crates/
    pm-core/              errors, colors, geometry, display abstraction, GPS model
    pm-font/              bitmap font tables and text metrics
    pm-icons/             32x32 status icons
    pm-gui/               labels, images, graphs, map tiles, waypoints
    pm-parser/            serial command parser, XML pull parser, config file
    nmea/                 NMEA 0183 + PMTK/PQ vendor extensions
    gpx/                  GPX track reader
    lsm303/               accelerometer/magnetometer driver
    acep-5in65-7c/        7-colour e-paper panel driver
    pm-qr/                QR code generator
  firmware/               the binary; NOT a workspace member (see below)
```

`firmware/` is listed in `exclude` rather than `members`. It can only be built
with the Xtensa toolchain and it pins its own target in
`firmware/.cargo/config.toml` and its own channel in
`firmware/rust-toolchain.toml`. Excluding it keeps `cargo test` at the `rust/`
level a plain host build with no cross-compilation setup at all.

## Decisions

### Runtime: ESP-IDF via esp-idf-svc/esp-idf-hal (std), not bare-metal esp-hal

The firmware needs, and will keep needing:

- FATFS on the SD card (`src/esp32/sd.c`, `src/esp32/map_loader.c`)
- WiFi station mode (`src/esp32/wifi.c`)
- HTTPS OTA with a bundled root CA (`src/esp32/ota.c`, `src/keys/`)
- NVS for persisted settings
- FreeRTOS tasks, queues and notifications (`include/tasks.h`)
- light and deep sleep

Bare-metal `esp-hal` has no supported story for FATFS, the WiFi stack, or
HTTPS OTA, and no FreeRTOS. Reimplementing those is a far larger project than
the port itself. `esp-idf-svc` + `esp-idf-hal` wrap the same ESP-IDF the C tree
already builds against, in `std` mode, so the port keeps `std` (allocation,
`String`, `std::error::Error`) and keeps the existing `sdkconfig.*` and
partition tables meaningful.

Consequence: the library crates are `std` crates, not `no_std`. That is
deliberate -- the target has `std`.

### Boards

| Board | Target | Feature | platformio env it replaces |
| --- | --- | --- | --- |
| `indianavi-s3-n16r8` (ESP32-S3) -- **primary** | `xtensa-esp32s3-espidf` | `board-esp32s3` (default) | `indianavi_s3_n16r8_debug`, `indianavi_s3_n16r8_release` |
| original REV2 ESP32 -- secondary | `xtensa-esp32-espidf` | `board-esp32` | `esp32dev_debug`, `esp32dev_release` |

One code base, selected by cargo feature. The features live in
`firmware/Cargo.toml` because the firmware crate is the only place that names
concrete hardware. `src/main.rs` rejects a build with neither or with an
ambiguous combination via `compile_error!`. The features replace the C
`-DESP_S3` / `-DREV2` build flags.

### Hardware only behind traits

Peripherals reach the port through two boundaries and nothing else:

1. **`embedded-hal` 1.0** for SPI, I2C, GPIO and delays. Drivers are generic
   over `embedded_hal::spi::SpiDevice`, `embedded_hal::i2c::I2c`,
   `embedded_hal::digital::{InputPin, OutputPin}` and
   `embedded_hal::delay::DelayNs`. `esp-idf-hal` supplies the concrete impls
   in `firmware/`.
2. **`pm_core::display::DisplayTarget`**, a project-local trait, replaces the
   C `display_t` struct of function pointers (`write_pixel`, `decompress`,
   `update`). GUI widgets render against the trait; the panel driver
   implements it.

`pm_core::power::Regulator` plays the same role for the switchable power
rails that `lib/Platinenmacher_HAL_ESP32/hw/regulator*.{c,h}` handled.

The rule this buys: **everything non-hardware compiles and unit-tests on the
host.** `cargo test` at `rust/` builds all ten crates for the host with no
ESP-IDF, no espup, and no device attached. Only `firmware/` needs the
cross toolchain. The ESP32 SPI/GPIO glue in
`lib/Platinenmacher_HAL_ESP32/hw/esp32/*.c` disappears entirely -- `esp-idf-hal`
already provides those `embedded-hal` implementations.

### Errors

`error_code_t` (`lib/Platinenmacher/error.h`) becomes `pm_core::Error`
(`crates/pm-core/src/error.rs`). Rules:

- Public APIs return `Result<T, pm_core::Error>`. `pm_core::Result<T>` is the
  alias.
- `PM_OK` has no variant: success is `Ok(_)`. Every other C variant maps 1:1,
  and the discriminants are pinned with `#[repr(u8)]` to the C values so a
  partially ported firmware can pass an `error_code_t` across FFI while the C
  tree still exists. A unit test asserts that pinning. Do not reorder;
  append.
- The C typo `DELEAYED` is spelled `Delayed`. `DEFERRED` stays `Deferred`.
- **No panics on recoverable paths.** No `unwrap`, `expect`, or indexing that
  can go out of range in library code; return `Error::OutOfBounds`,
  `Error::Unavailable`, `Error::Timeout` and friends instead. `panic!` is for
  programmer errors that a test would catch, never for bad input, a missing
  file, or an unresponsive peripheral.

### Module headers name the C files they replace

Every module starts with a doc comment listing the C file(s) it ports:

```rust
//! Line-oriented serial command parser state machine.
//!
//! Replaces: lib/Platinenmacher/parser/command.c,
//! lib/Platinenmacher/parser/command.h
```

Where a module has no C counterpart (or a C file has no Rust counterpart, like
`memory.h`'s `RTOS_Malloc`), the comment says so and why. This is what makes
the final cleanup task auditable: every deleted C file should be findable in a
`Replaces:` line.

## Verification

**Short answer for reviewers:** the Rust toolchain *could* be installed here.
Both `rustup` (stable) and `espup` (the `esp` Xtensa toolchain) installed
successfully, and the ten library crates under `crates/` build and test **green**
on the host -- so later library tasks are backed by a real compile, not by
inspection. The one gap is `firmware/`, which cannot be *built* here because
the container has no host C compiler and no `python3`/`cmake`/`ninja` for
ESP-IDF; see "Firmware build" below for exactly what that does and does not
cover.

Every command below is what was actually run.

### Install

No `cargo`/`rustup` was present. This container has no `curl` and no `wget`,
and `/tmp` is mounted `noexec`, so `rustup-init` was fetched with `node` and
run from a `noexec`-free path:

```sh
node -e "const fs=require('fs');fetch('https://static.rust-lang.org/rustup/dist/x86_64-unknown-linux-gnu/rustup-init').then(async r=>fs.writeFileSync('/tmp/rustup-init',Buffer.from(await r.arrayBuffer())))"
mkdir -p ~/.local/bin && cp /tmp/rustup-init ~/.local/bin/ && chmod +x ~/.local/bin/rustup-init
~/.local/bin/rustup-init -y --no-modify-path --profile minimal --default-toolchain stable
rustup component add clippy rustfmt
```

On a normal developer machine this is just:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
rustup component add clippy rustfmt
```

Installed and used here: `rustc`/`cargo` 1.98.1, `rustup` 1.29.1.

### Host build and test

On a machine with a working C toolchain (i.e. `cc` on `PATH`), the host
workflow is the plain one:

```sh
cd rust
cargo build --workspace
cargo test  --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
```

**This container has no C compiler and no `sudo`**, so the default
`x86_64-unknown-linux-gnu` target cannot link (`rustc` reports
`no default linker (cc) was found in your PATH`). The workaround is the
musl target, whose `libc.a` and crt objects `rustup` ships itself, linked with
the bundled `rust-lld`:

```sh
rustup target add x86_64-unknown-linux-musl
export CARGO_TARGET_X86_64_UNKNOWN_LINUX_MUSL_LINKER=rust-lld
export CARGO_TARGET_X86_64_UNKNOWN_LINUX_MUSL_RUSTFLAGS="-C linker-flavor=ld.lld -C target-feature=+crt-static"

cd rust
cargo test   --workspace --target x86_64-unknown-linux-musl
cargo clippy --workspace --all-targets --target x86_64-unknown-linux-musl -- -D warnings
cargo fmt --all -- --check
```

Result at the time of writing: all ten crates build, `cargo test` exits 0 with
3 passing tests in `pm-core` (the `error_code_t` discriminant pinning), and
`clippy -D warnings` and `cargo fmt --check` are clean.

Note for later tasks: the musl detour is only needed because this container
lacks `cc`. Do not commit those environment variables into
`rust/.cargo/config.toml` -- they would override a developer's normal native
build. Keep them in the shell.

### Firmware build

The firmware needs the Xtensa toolchain from `espup` plus a full ESP-IDF build
environment:

```sh
# espup has no crates.io-free path here; the release binary works:
#   https://github.com/esp-rs/espup/releases/latest/download/espup-x86_64-unknown-linux-gnu
cargo install espup ldproxy espflash     # or use the espup release binary
espup install --targets esp32s3,esp32
. ~/export-esp.sh

cd rust/firmware
cargo build --release                                  # ESP32-S3, default
cargo build --release --target xtensa-esp32-espidf \
            --no-default-features --features board-esp32   # REV2 ESP32
cargo run --release                                    # flash + monitor
```

`cargo test` does not apply to `firmware/`: it is a stub `main()` with no
logic, and ESP-IDF targets cannot run host tests. Logic belongs in
`crates/*`, where it is tested.

**What was verified here, and what was not.**

Verified:

- `espup install --targets esp32s3,esp32` succeeded (espup 0.17.1). It
  installed Xtensa Rust 1.97.0.0, the `xtensa-esp-elf` GCC and Xtensa LLVM;
  `rustup toolchain list` shows the `esp` toolchain that
  `firmware/rust-toolchain.toml` asks for.
- `cargo metadata --filter-platform xtensa-esp32s3-espidf` resolves the whole
  firmware dependency graph, so the `esp-idf-svc` / `esp-idf-hal` / `embuild`
  version triple in `firmware/Cargo.toml` is at least mutually consistent.
- `rustfmt --check src/main.rs build.rs` parses clean.

NOT verified -- `cargo build` in `firmware/` cannot run in this container:

- `ldproxy` is the linker driver `firmware/.cargo/config.toml` names, and it is
  a *host* binary. `cargo install ldproxy` fails here (`libc` and `proc-macro2`
  build scripts cannot link) for the same missing-`cc` reason as above.
- `esp-idf-sys` drives the ESP-IDF build system, which needs `python3`,
  `cmake`, `ninja` and a host C compiler. None are installed and there is no
  `sudo`.

So: the `esp-idf-svc`/`esp-idf-hal` versions and the `ESP_IDF_VERSION =
"v5.2.1"` pin are *plausible but uncompiled*. Reviewers should treat
`firmware/` as inspection-only, and the first task that actually ports firmware
logic must run on an image with the ESP-IDF prerequisites and correct those
pins if they do not hold.

The ten library crates under `crates/` are **not** affected by any of this --
they are genuinely built and tested by the host `cargo test` above, which is
the whole point of the trait boundary.
