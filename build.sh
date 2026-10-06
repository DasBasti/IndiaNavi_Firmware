#!/usr/bin/env bash
# Build the IndiaNavi firmware with PlatformIO and optionally flash it.
set -euo pipefail

usage() {
    cat <<EOF
Usage: $(basename "$0") [debug|release] [options]

Build type (default: debug):
  debug               Build the debug firmware
  release             Build the release firmware

Options:
  --install           Flash the firmware to the device after building
  --board <name>      Board prefix of the PlatformIO env (default: indianavi_s3_n16r8)
                      e.g. esp32dev
  --port <device>     Serial port used for --install (default: from platformio.ini)
  --monitor           Open the serial monitor after installing
  --clean             Clean the build directory before building
  -h, --help          Show this help
EOF
}

build_type="debug"
board="indianavi_s3_n16r8"
install=0
monitor=0
clean=0
port=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release) build_type="$1" ;;
        --debug) build_type="debug" ;;
        --release) build_type="release" ;;
        --install) install=1 ;;
        --monitor) monitor=1 ;;
        --clean) clean=1 ;;
        --board) board="${2:?--board needs a value}"; shift ;;
        --port) port="${2:?--port needs a value}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
    shift
done

cd "$(dirname "$(readlink -f "$0")")"

if command -v pio >/dev/null 2>&1; then
    pio=pio
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    pio="$HOME/.platformio/penv/bin/pio"
else
    echo "PlatformIO (pio) not found. Install it with: pip install platformio" >&2
    exit 1
fi

env="${board}_${build_type}"
if ! grep -q "^\[env:${env}\]" platformio.ini; then
    echo "Environment '${env}' not found in platformio.ini" >&2
    exit 1
fi

if [[ $clean -eq 1 ]]; then
    "$pio" run -e "$env" -t clean
fi

echo "==> Building ${env}"
"$pio" run -e "$env"

if [[ $install -eq 1 ]]; then
    echo "==> Installing ${env}"
    upload_args=(-e "$env" -t upload)
    [[ -n "$port" ]] && upload_args+=(--upload-port "$port")
    "$pio" run "${upload_args[@]}"

    if [[ $monitor -eq 1 ]]; then
        monitor_args=(-e "$env")
        [[ -n "$port" ]] && monitor_args+=(--port "$port")
        "$pio" device monitor "${monitor_args[@]}"
    fi
fi

echo "==> Firmware: .pio/build/${env}/firmware.bin"
