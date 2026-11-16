#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if ! command -v idf.py >/dev/null 2>&1; then
    source "$HOME/esp/esp-idf/export.sh"
fi
idf.py build
if [[ "${1:-}" == "--rom" ]]; then
    [[ $# -eq 2 ]] || { echo "Usage: $0 --rom /dev/ttyACM1" >&2; exit 2; }
    exec "$ROOT/tools/flash-rom-download.sh" "$2"
elif [[ $# -eq 1 ]]; then
    port="$1"
    [[ -c "$port" ]] || { echo "Not a serial device: $port" >&2; exit 2; }
    idf.py -p "$port" flash
    echo "Flashed; no USB monitor started."
elif [[ $# -eq 0 ]]; then
    echo 'Build complete. For the known USB reset problem use --rom /dev/ttyACM1 after entering download mode.'
else
    echo "Usage: $0 [--rom /dev/ttyACM1 | /dev/ttyACM1]" >&2
    exit 2
fi
