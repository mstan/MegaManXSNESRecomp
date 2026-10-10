#!/usr/bin/env bash
set -euo pipefail

export PATH="/c/msys64/mingw64/bin:$PATH"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
source_dir="$repo_root/_private/saves"
output_dir="$repo_root/build-mingw/saves"
converter="$script_dir/convert_old_fixture.py"

if [[ ! -d "$source_dir" ]]; then
  printf 'FAIL: private fixture directory is missing: %s\n' "$source_dir" >&2
  exit 2
fi
if [[ ! -f "$converter" ]]; then
  printf 'FAIL: fixture converter is missing: %s\n' "$converter" >&2
  exit 2
fi

shopt -s nullglob
sources=("$source_dir"/*.sav)
if [[ ${#sources[@]} -eq 0 ]]; then
  printf 'FAIL: no private .sav fixtures found in: %s\n' "$source_dir" >&2
  exit 2
fi

mkdir -p "$output_dir"
for source in "${sources[@]}"; do
  destination="$output_dir/$(basename "$source")"
  python -I "$converter" "$source" "$destination"
done

printf 'PASS: prepared %u fixture saves in %s\n' "${#sources[@]}" "$output_dir"
