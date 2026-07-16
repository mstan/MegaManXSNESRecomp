#!/usr/bin/env bash
# Initialize and verify every dependency needed by this checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! git -C "$ROOT" rev-parse --show-toplevel >/dev/null 2>&1; then
  echo "bootstrap.sh: $ROOT is not a Git checkout" >&2
  exit 1
fi

echo "=== Synchronizing submodule URLs ==="
git -C "$ROOT" submodule sync --recursive

echo "=== Initializing pinned submodules ==="
git -C "$ROOT" submodule update --init --recursive

expected="$(git -C "$ROOT" ls-files --stage -- snesrecomp | awk '$1 == "160000" { print $2 }')"
actual="$(git -C "$ROOT/snesrecomp" rev-parse HEAD 2>/dev/null || true)"
if [ -z "$expected" ] || [ "$actual" != "$expected" ]; then
  echo "bootstrap.sh: snesrecomp is at ${actual:-<missing>}, expected ${expected:-<missing gitlink>}" >&2
  exit 1
fi

status="$(git -C "$ROOT" submodule status --recursive)"
if printf '%s\n' "$status" | grep -Eq '^[+-U]'; then
  echo "bootstrap.sh: one or more submodules are missing or at the wrong revision:" >&2
  printf '%s\n' "$status" >&2
  exit 1
fi

if [ ! -f "$ROOT/snesrecomp/runner/runner.cmake" ] ||
   [ ! -f "$ROOT/snesrecomp/tools/v2_emit.py" ]; then
  echo "bootstrap.sh: the pinned snesrecomp checkout is incomplete" >&2
  exit 1
fi

printf '\nReady: snesrecomp %s and all nested submodules are initialized.\n' "$actual"
printf 'Next: stage your legally obtained ROM and run bash tools/regen.sh usa --no-tests\n'
