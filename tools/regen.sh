#!/usr/bin/env bash
# Regen pipeline driver for MegaManXSNESRecomp.
#
# Regenerates the selected regional variant from its bank configs and verified
# ROM, then syncs that variant's funcs.h.
#
# Variants:
#   usa (default)           Mega Man X (USA v1.1)
#   jp                      Rockman X (Japan v1.1)
#   all                     regenerate both variants
#
# Flags:
#   --no-tests             skip the framework test suite (default: run it).
#   --strict-idempotent    regenerate into a temporary directory and require
#                          byte-identical output.
#   -h | --help            this message.
#
# Run from anywhere - paths resolve relative to this script's location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_TESTS=1
STRICT_IDEMPOTENT=0
VARIANT="usa"
VARIANT_SEEN=0
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
    -h|--help)  sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    usa|jp|all)
      if [ "$VARIANT_SEEN" -eq 1 ]; then
        echo "regen.sh: select only one of usa, jp, or all" >&2
        exit 2
      fi
      VARIANT="$arg"
      VARIANT_SEEN=1
      ;;
    *) echo "regen.sh: unknown argument: $arg (try --help)" >&2; exit 2 ;;
  esac
done

cd "$ROOT"

TESTS="snesrecomp/tests/run_tests.py"

# Python interpreter: prefer python3 (macOS / most Linux have no bare `python`).
PYTHON="${PYTHON:-$(command -v python3 || command -v python || true)}"
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3/python interpreter found on PATH" >&2
  exit 1
fi

step() { echo; echo "=== $* ==="; }

regen_variant() {
  local name="$1" rom cfg_dir out_dir funcs_h tmp_gen
  local -a emit_extra=()
  case "$name" in
    usa)
      rom="mmx.sfc"
      cfg_dir="recomp"
      out_dir="src/gen"
      funcs_h="recomp/funcs.h"
      ;;
    jp)
      rom="variants/jp/roms/rockmanx.sfc"
      cfg_dir="variants/jp/config"
      out_dir="variants/jp/gen"
      funcs_h="variants/jp/config/funcs.h"
      # Rockman shares handwritten host sources with the USA variant, whose
      # optional HLE scheduler contains region-specific addresses. Its own
      # clean LLE observations are the authoritative AOT optimization profile.
      emit_extra=(--no-host-root-scan
                  --profile-manifest variants/jp/tier2_coverage.json)
      ;;
  esac

  if [ ! -f "$rom" ]; then
    echo "regen.sh: $rom not found - stage the verified $name ROM first." >&2
    exit 1
  fi

  step "Regenerating $name banks"
  # The LLE-first emitter publishes a complete staging directory atomically.
  # Missing AOT coverage remains authoritative ROM execution through LLE.
  # --cfg-roots is the static-coverage policy (2026-07-16): every declared
  # `func` seeds the analysis closure so the proven surface is materialized
  # as AOT; the interpreter is a failsafe for the unprovable remainder,
  # never the plan of record for known code.
  "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$rom" \
      --cfg-dir "$cfg_dir" --out-dir "$out_dir" --cfg-roots \
      "${emit_extra[@]}"

  step "Syncing $name funcs.h"
  "$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir "$cfg_dir" \
      --out "$funcs_h"

  if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
    step "Checking $name idempotency"
    tmp_gen="$(mktemp -d)"
    "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$rom" \
        --cfg-dir "$cfg_dir" --out-dir "$tmp_gen" --cfg-roots \
        "${emit_extra[@]}"
    "$PYTHON" snesrecomp/tools/v2_compare_output.py \
        --expected "$out_dir" --actual "$tmp_gen"
    rm -rf "$tmp_gen"
  fi
}

if [ "$VARIANT" = "all" ]; then
  regen_variant usa
  regen_variant jp
else
  regen_variant "$VARIANT"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
