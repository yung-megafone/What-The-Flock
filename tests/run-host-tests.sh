#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$ROOT/main" \
  "$ROOT/tests/test_signal_math.c" -o "$TMP/wtf-math-tests"
"$TMP/wtf-math-tests"
cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$ROOT/main" \
  "$ROOT/tests/test_scan_math.c" -o "$TMP/wtf-scan-tests"
"$TMP/wtf-scan-tests"

cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$ROOT/main" \
  "$ROOT/tests/test_lcd_frame.c" "$ROOT/main/wtf_lcd_frame.c" -o "$TMP/wtf-lcd-tests"
"$TMP/wtf-lcd-tests"

python3 "$(dirname "$0")/test_lab_config.py"
