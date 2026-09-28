# Testing `rust/firmware`

`rust/PORTING.md` says `cargo test` "does not apply to `firmware/`". That was
true of the scaffold's stub `main()`. It stopped being true with `src/board/`:
the board support layer contains real logic -- the battery gauge, the regulator
refcounting and polarity, the pin tables, the button decoding -- and all of it
is tested on the host.

## Running the tests

`.cargo/config.toml` sets `[build] target = "xtensa-esp32s3-espidf"`, which is
the right default for building the firmware and the wrong one for running
tests. `--target` on the command line overrides it, and `+stable` overrides the
`channel = "esp"` in `rust-toolchain.toml`, so no Xtensa toolchain is needed:

```sh
cd rust/firmware
cargo +stable test   --target x86_64-unknown-linux-gnu
cargo +stable clippy --target x86_64-unknown-linux-gnu --all-targets -- -D warnings
cargo +stable fmt -- --check
```

Both boards, because the pin map is the thing most likely to rot:

```sh
cargo +stable test --target x86_64-unknown-linux-gnu
cargo +stable test --target x86_64-unknown-linux-gnu \
      --no-default-features --features board-esp32
```

Result at the time of writing: **69 tests, green on both boards**; `clippy -D
warnings` and `cargo fmt --check` clean. `rustc`/`cargo` 1.98.1 from `rustup`,
with `gcc` on `PATH` for the link step.

## Why this works at all

Three things make a host `cargo test` possible in a crate whose whole purpose
is an ESP32:

1. **`esp-idf-svc`/`esp-idf-hal` are target-gated.** `Cargo.toml` declares them
   under `[target.'cfg(target_os = "espidf")'.dependencies]`, so a host build
   never resolves them, never runs `esp-idf-sys`, and never needs
   python3/cmake/ninja.
2. **Every module that touches ESP-IDF confines it to an inner
   `#[cfg(target_os = "espidf")] mod espidf`** and re-exports it. Everything
   outside those inner modules is pure Rust over `embedded-hal` traits.
3. **`build.rs` checks `CARGO_CFG_TARGET_OS` at run time.** A build script is
   always compiled for the *host*, so it cannot be target-gated the way
   dependencies can -- `embuild` stays an unconditional build-dependency and the
   script simply does nothing when the target is not `espidf`.

What that buys, and what it does not: the pure logic is genuinely compiled and
executed. The `mod espidf` blocks are **not compiled anywhere** -- not here, not
in CI. Review them by reading. Every value they hand to ESP-IDF comes from a
`const` that a host test pins, so the numbers are checked even though the calls
are not. `rust/PORTING.md`, "Firmware build", explains why an ESP-IDF build
cannot run in this container.

## Building the firmware

Unchanged from `rust/PORTING.md`:

```sh
espup install --targets esp32s3,esp32
. ~/export-esp.sh

cd rust/firmware
cargo build --release                                      # ESP32-S3, default
cargo build --release --target xtensa-esp32-espidf \
            --no-default-features --features board-esp32    # REV2 ESP32
cargo run --release                                        # flash + monitor
```
