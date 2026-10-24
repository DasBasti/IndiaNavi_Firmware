# PlatformIO build script: builds the mapsforge-rs vector map library with cargo and links it.
#
# - ESP32 boards: PlatformIO compiles the ESP-IDF components itself and does not run the custom
#   CMake targets of components/mapsforge, so the Rust static library is built and linked here
#   (the component's CMake file compiles the C rasterizer and defines VECTOR_MAP). Needs the
#   Xtensa Rust toolchain: `cargo install espup && espup install`.
# - linux_native (simulator): builds the library for the PC, compiles the C rasterizer and defines
#   VECTOR_MAP, the simulator then draws MAPS/map.map of its SD card directory.
#
# Without a mapsforge-rs checkout nothing is linked and the firmware uses tile images only. The
# checkout is MAPSFORGE_RS_DIR from the environment or ../IndiaNavi_Firmware.rs/mapsforge-rs.
import os
import subprocess

Import("env", "projenv")  # noqa: F821  (projenv compiles the project sources)

TRIPLES = {
    "esp32s3": "xtensa-esp32s3-none-elf",
    "esp32": "xtensa-esp32-none-elf",
}

project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
build_dir = env.subst("$BUILD_DIR")  # noqa: F821
checkout = os.path.abspath(
    os.environ.get(
        "MAPSFORGE_RS_DIR",
        os.path.join(project_dir, "..", "IndiaNavi_Firmware.rs", "mapsforge-rs"),
    )
)
board = env.get("BOARD")  # noqa: F821, the native platform has none
mcu = env.BoardConfig().get("build.mcu", "") if board else ""  # noqa: F821
triple = TRIPLES.get(mcu)
target_dir = os.path.join(build_dir, "mapsforge-rs")

if os.path.exists(os.path.join(checkout, "Cargo.toml")):
    if triple:
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
    elif not board:
        print("Building mapsforge-rs for the PC")
        subprocess.check_call(
            ["cargo", "build", "--release", "--features", "ffi", "--target-dir", target_dir],
            cwd=checkout,
        )
        os.makedirs(build_dir, exist_ok=True)
        raster_object = os.path.join(build_dir, "mfr_raster.o")
        raster_lib = os.path.join(build_dir, "libmfr_raster.a")
        subprocess.check_call(
            ["gcc", "-O2", "-c", os.path.join(checkout, "c", "mfr_raster.c"),
             "-I" + os.path.join(checkout, "include"), "-I" + os.path.join(checkout, "c"),
             "-o", raster_object]
        )
        subprocess.check_call(["ar", "rcs", raster_lib, raster_object])
        projenv.Append(  # noqa: F821
            CPPDEFINES=["VECTOR_MAP"],
            CPPPATH=[os.path.join(checkout, "include"), os.path.join(checkout, "c")],
        )
        env.Append(  # noqa: F821
            LIBPATH=[build_dir, os.path.join(target_dir, "release")],
            # the static Rust library needs the C libraries of std, the rasterizer comes first
            LIBS=["mfr_raster", "mapsforge_rs", "pthread", "dl", "m"],
        )
