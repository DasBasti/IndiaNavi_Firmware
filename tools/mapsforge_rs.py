# PlatformIO build script: builds the mapsforge-rs vector map library with cargo and links it.
#
# PlatformIO compiles the ESP-IDF components itself and does not run the custom CMake targets of
# components/mapsforge, so the Rust static library is built and linked here. The component's CMake
# file compiles the C part and defines VECTOR_MAP. Without a mapsforge-rs checkout nothing is
# linked and the firmware uses tile images only.
#
# The checkout is MAPSFORGE_RS_DIR from the environment or ../IndiaNavi_Firmware.rs/mapsforge-rs.
# Needs the Xtensa Rust toolchain: `cargo install espup && espup install`.
import os
import subprocess

Import("env")  # noqa: F821

TRIPLES = {
    "esp32s3": "xtensa-esp32s3-none-elf",
    "esp32": "xtensa-esp32-none-elf",
}

project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
checkout = os.environ.get(
    "MAPSFORGE_RS_DIR",
    os.path.join(project_dir, "..", "IndiaNavi_Firmware.rs", "mapsforge-rs"),
)
try:
    mcu = env.BoardConfig().get("build.mcu", "")  # noqa: F821
except Exception:  # the native simulator has no board
    mcu = ""
triple = TRIPLES.get(mcu)

if os.path.exists(os.path.join(checkout, "Cargo.toml")) and triple:
    target_dir = os.path.join(env.subst("$BUILD_DIR"), "mapsforge-rs")  # noqa: F821
    print("Building mapsforge-rs for %s" % triple)
    subprocess.check_call(
        [
            "rustup", "run", "esp", "cargo", "build", "--release",
            "-Zbuild-std=core,alloc", "--target", triple,
            "--no-default-features", "--features", "ffi",
            "--target-dir", target_dir,
        ],
        cwd=checkout,
    )
    env.Append(  # noqa: F821
        LIBPATH=[os.path.join(target_dir, triple, "release")],
        LIBS=["mapsforge_rs"],
    )
