#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$ROOT/main" \
  "$ROOT/tests/test_signal_math.c" -o "$TMP/wtf-math-tests"
"$TMP/wtf-math-tests"
