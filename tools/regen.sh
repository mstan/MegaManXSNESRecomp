#!/usr/bin/env bash
# Regen pipeline driver for MegaManXSNESRecomp.
#
# Regenerates src/gen/*.c from the recomp/bank_*.cfg configs over a verified
# mmx.sfc, then syncs recomp/funcs.h. Mirrors SuperMarioWorldRecomp/tools/regen.sh.
#
# Flags:
#   --no-tests   skip the framework test suite (default: run it).
#   -h | --help  this message.
#
# Run from anywhere — paths resolve relative to this script's location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_TESTS=1
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    -h|--help)  sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    *) echo "regen.sh: unknown flag: $arg (try --help)" >&2; exit 2 ;;
  esac
done

cd "$ROOT"

ROM="mmx.sfc"
TESTS="snesrecomp/tests/run_tests.py"

# Python interpreter: prefer python3 (macOS / most Linux have no bare `python`).
PYTHON="${PYTHON:-$(command -v python3 || command -v python || true)}"
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3/python interpreter found on PATH" >&2
  exit 1
fi

if [ ! -f "$ROM" ]; then
  echo "regen.sh: $ROM not found at repo root — drop a verified MMX ROM there." >&2
  exit 1
fi

step() { echo; echo "=== $* ==="; }

step "Regenerating banks"
"$PYTHON" snesrecomp/tools/v2_regen.py --rom "$ROM" \
    --cfg-dir recomp --out-dir src/gen

step "Syncing funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
