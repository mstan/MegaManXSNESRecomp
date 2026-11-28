#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec sh "${SNESRECOMP_ROOT:-$ROOT/snesrecomp}/tools/release/test_appimage_layout.sh" "$@"
