#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${SNESRECOMP_ROOT:-$ROOT/snesrecomp}"
exec bash "$ENGINE/tools/release/build_linux.sh" --project-root "$ROOT" "$@"
