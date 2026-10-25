#!/usr/bin/env bash
# Builds the simulator and saves a screenshot of the map screen as PNG, to iterate on the map style.
#   tools/sim_screenshot.sh <map file> <lat> <lon> [zoom 14|16] [out.png] [simulator options, e.g. --no-fix]
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

map="${1:?map file}"; lat="${2:?latitude}"; lon="${3:?longitude}"; zoom="${4:-16}"; out="${5:-frame.png}"
pio="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"

"$pio" run -e linux_native > /dev/null
.pio/build/linux_native/program --map "$map" --zoom "$zoom" --screenshot "${out%.png}.pnm" "${@:6}" "$lat" "$lon" > /dev/null
python3 -c "from PIL import Image; Image.open('${out%.png}.pnm').save('$out')"
rm -f "${out%.png}.pnm"
echo "$out"
