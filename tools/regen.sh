#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${SNESRECOMP_ROOT:-$ROOT/snesrecomp}"
variant=usa; options=()
for arg in "$@"; do
  case "$arg" in usa|jp|all) variant="$arg";; *) options+=("$arg");; esac
done
regen() {
  if [ "$1" = jp ]; then
    sh "$ENGINE/tools/regen_project.sh" --project-root "$ROOT" --cfg-dir variants/jp/config "${options[@]}"
  else
    sh "$ENGINE/tools/regen_project.sh" --project-root "$ROOT" "${options[@]}"
  fi
}
if [ "$variant" = all ]; then regen usa; regen jp; else regen "$variant"; fi
