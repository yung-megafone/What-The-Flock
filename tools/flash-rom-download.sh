#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-}"
if [[ -z "$PORT" || ! -c "$PORT" ]]; then
    echo "Usage: $0 /dev/ttyACM1  (port must exist)" >&2
    exit 2
fi
if [[ ! -f "$ROOT/build/what-the-flock.bin" || \
      ! -f "$ROOT/build/bootloader/bootloader.bin" || \
      ! -f "$ROOT/build/partition_table/partition-table.bin" ]]; then
    echo "Build binaries not found. Run idf.py build first." >&2
    exit 2
fi
if [[ -z "${IDF_PATH:-}" ]]; then
    source "$HOME/esp/esp-idf/export.sh"
fi
cd "$ROOT"
cat <<'NOTICE'
This uses the ROM downloader (not the crashing application USB console).
BEFORE continuing: hold BOOT, tap RESET, release BOOT on the XIAO C5.
The flashing command does not erase the SPIFFS partition. On first application boot, firmware WILL format storage if and ONLY if the entire partition is erased (all 0xFF).
NOTICE
read -r -p "Board is in BOOT/download mode? Press Enter (Ctrl+C cancels): " _
python -m esptool --chip esp32c5 -p "$PORT" -b 115200 \
    --before no_reset --after no_reset --no-stub \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x2000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/what-the-flock.bin
printf '\nFlash completed. Disconnect D10 for normal mode, or jumper D10 to GND for diagnostics.\n'
printf 'Press physical RESET to exit download mode. Do not open a serial monitor.\n'
