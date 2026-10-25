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
# checkout is MAPSFORGE_RS_DIR from the environment or the submodule lib/mapsforge-rs.
import os
import shutil
import subprocess
import sys

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
        os.path.join(project_dir, "lib", "mapsforge-rs"),
    )
)
board = env.get("BOARD")  # noqa: F821, the native platform has none
mcu = env.BoardConfig().get("build.mcu", "") if board else ""  # noqa: F821
triple = TRIPLES.get(mcu)
target_dir = os.path.join(build_dir, "mapsforge-rs")



def require(ok, what):
    if not ok:
        sys.stderr.write(
            "\nmapsforge-rs needs %s.\n"
            "See 'Building' in the README: install Rust (https://rustup.rs) and the Xtensa toolchain\n"
            "(`cargo install espup && espup install --targets esp32s3`), and fetch the sources with\n"
            "`git submodule update --init --recursive`.\n\n" % what
        )
        env.Exit(1)  # noqa: F821


def has_esp_toolchain():
    if not shutil.which("rustup"):
        return False
    result = subprocess.run(["rustup", "toolchain", "list"], capture_output=True, text=True)
    return any(line.split()[0] == "esp" for line in result.stdout.splitlines() if line.strip())


if not os.path.exists(os.path.join(checkout, "Cargo.toml")):
    print("mapsforge-rs not found in %s (git submodule update --init), building without vector map" % checkout)
else:
    if triple:
        require(has_esp_toolchain(), "the Rust toolchain 'esp' (Xtensa)")
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
        # relink when cargo rebuilt the library, SCons does not look into the libraries by itself
        env.Depends("$PROG_PATH", os.path.join(target_dir, triple, "release", "libmapsforge_rs.a"))  # noqa: F821
    elif not board:
        require(shutil.which("cargo") and shutil.which("gcc"), "cargo and gcc")
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
        # relink when the libraries were rebuilt, SCons does not look into the libraries by itself
        env.Depends("$PROG_PATH", [raster_lib, os.path.join(target_dir, "release", "libmapsforge_rs.a")])  # noqa: F821
