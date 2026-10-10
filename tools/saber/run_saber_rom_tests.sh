#!/usr/bin/env bash
# Run the Saber-owned real-ROM characterization executable in a disposable
# executable/catalog/cache directory. Usage: tools/saber/run_saber_rom_tests.sh
# [X1_ROM]. The default X1 ROM and fixture are the repository's documented
# local assets; MMX_COOP_X3_ROM and MMX_ZERO_TEST_FIXTURE may override them.

set -o pipefail

export PATH="/c/msys64/mingw64/bin:$PATH"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
build_dir="$repo_root/build-mingw"
tmp_dir="$build_dir/tmp"
exe="$build_dir/mmx_saber_rom_tests.exe"
catalog_source="$build_dir/mods/preloaded"
cache_source="$build_dir/cache"
private_fixture_dir="$repo_root/_private/saves"
fixture_dir="$build_dir/saves"
prepare_fixtures="$repo_root/tools/saber/prepare_fixtures.sh"
x1_rom="${1:-$repo_root/mmx.sfc}"
x3_rom="${MMX_COOP_X3_ROM:-$repo_root/Mega Man X3 (USA).sfc}"
fixture="${MMX_ZERO_TEST_FIXTURE:-$build_dir/saves/save0.sav}"

if [[ ! -d "$private_fixture_dir" ]]; then
  printf 'FAIL: private fixture directory is missing: %s\n' "$private_fixture_dir" >&2
  exit 2
fi
shopt -s nullglob
private_fixtures=("$private_fixture_dir"/*.sav)
if [[ ${#private_fixtures[@]} -eq 0 ]]; then
  printf 'FAIL: no private .sav fixtures found in: %s\n' "$private_fixture_dir" >&2
  exit 2
fi
prepare_needed=0
for private_fixture in "${private_fixtures[@]}"; do
  working_fixture="$fixture_dir/$(basename "$private_fixture")"
  if [[ ! -f "$working_fixture" || "$private_fixture" -nt "$working_fixture" ]]; then
    prepare_needed=1
    break
  fi
done
if [[ "$prepare_needed" == 1 ]]; then
  bash "$prepare_fixtures"
fi

if [[ $# -gt 1 ]]; then
  printf 'usage: %s [X1_ROM]\n' "$0" >&2
  exit 2
fi
# Some groups (saber-assets, the Saber input/attack groups) only run when
# asked by name. A full run executes the default pass plus every named group,
# each in its own isolated catalog/cache copy. Add new groups to this list.
named_groups=(saber-assets saber-input saber-ground-1 saber-ground-combo saber-air saber-wall saber-dash saber-cancel saber-land saber-ground-hit saber-render-snapshot saber-wave-render
  saber-ground-lifecycle saber-lifecycle-load saber-contexts saber-buster-rules saber-burst-height saber-finisher saber-priority-classify saber-priority saber-armadillo saber-tuning saber-hitbox-debug saber-damage saber-wave-travel saber-wave-damage saber-wave-lifecycle saber-ride-pilot saber-boss-death zero-extension zero-hook-parity zero-response-seam saber-package x3-zero-specials fixtures)
if [[ -z "${MMX_SABER_TEST_ONLY:-}" && -z "${MMX_SABER_RUNNER_PASS:-}" ]]; then
  failed=()
  MMX_SABER_RUNNER_PASS=1 bash "${BASH_SOURCE[0]}" "$@" || failed+=(default)
  for group in "${named_groups[@]}"; do
    if [[ "$group" == zero-response-seam ]]; then
      env -u SNESRECOMP_LLE_BOUNCE MMX_SABER_RUNNER_PASS=1 \
        MMX_SABER_TEST_ONLY="$group" bash "${BASH_SOURCE[0]}" "$@" ||
        failed+=("$group")
      SNESRECOMP_LLE_BOUNCE=0 MMX_SABER_RUNNER_PASS=1 \
        MMX_SABER_TEST_ONLY="$group" bash "${BASH_SOURCE[0]}" "$@" ||
        failed+=("${group}-interpreted")
    else
      MMX_SABER_RUNNER_PASS=1 MMX_SABER_TEST_ONLY="$group" \
        bash "${BASH_SOURCE[0]}" "$@" || failed+=("$group")
    fi
  done
  if [[ ${#failed[@]} -eq 0 ]]; then
    printf 'PASS: all Saber ROM passes (default + %s named groups)\n' \
      "${#named_groups[@]}"
    exit 0
  fi
  printf 'FAIL: Saber ROM passes failed: %s\n' "${failed[*]}"
  exit 1
fi
if [[ ! -x "$exe" ]]; then
  printf 'FAIL: test executable is missing: %s\n' "$exe" >&2
  exit 2
fi
if [[ ! -d "$catalog_source" ]]; then
  printf 'FAIL: staged catalog is missing: %s\n' "$catalog_source" >&2
  exit 2
fi
for required in "$x1_rom" "$x3_rom" "$fixture"; do
  if [[ ! -f "$required" ]]; then
    printf 'FAIL: required ROM/fixture is missing: %s\n' "$required" >&2
    exit 2
  fi
done

mkdir -p "$tmp_dir"
run_name="saber-rom-$(date +%Y%m%d-%H%M%S)-$$"
run_dir="$(mktemp -d "$tmp_dir/$run_name.XXXXXX")"
catalog_copy="$run_dir/catalog"
mkdir -p "$catalog_copy"
cp -a "$catalog_source/." "$catalog_copy/"
find "$catalog_copy" -type f -name 'state.toml' -delete
if [[ -d "$cache_source" ]]; then
  cp -a "$cache_source" "$run_dir/cache"
else
  mkdir -p "$run_dir/cache"
fi
empty_cache="$run_dir/empty-cache"
mkdir -p "$empty_cache"
if [[ -f "$build_dir/SDL3.dll" ]]; then
  cp "$build_dir/SDL3.dll" "$run_dir/SDL3.dll"
fi
cp "$exe" "$run_dir/mmx_saber_rom_tests.exe"

log_path="$tmp_dir/$run_name.log"
test_exe="$run_dir/mmx_saber_rom_tests.exe"
asset_path="$run_dir/cache/mmx-source/x3-zero-v7.bin"

set +e
(
  cd "$tmp_dir" || exit 125
  TMP="$run_dir" TEMP="$run_dir" TMPDIR="$run_dir" \
    MMX_COOP_LAUNCHER_ROOT="$catalog_copy" \
    MMX_COOP_X3_ROM="$x3_rom" \
    MMX_ZERO_TEST_FIXTURE="$fixture" \
    MMX_SABER_FIXTURE_DIR="$fixture_dir" \
    MMX_ZERO_TEST_ASSETS="$asset_path" \
    MMX_SABER_TEST_CACHE="$run_dir/cache" \
    MMX_SABER_EMPTY_CACHE="$empty_cache" \
    MMX_SABER_TEST_CACHE_ONLY="1" \
    "$test_exe" "$x1_rom"
) 2>&1 | tee "$log_path"
test_exit="${PIPESTATUS[0]}"
set -e

if [[ "$test_exit" == 0 ]] && [[ "${MMX_SABER_TEST_ONLY:-}" == fixtures ]] &&
    grep -Eiq 'bad game chunk|Save (read error|file .* bad magic/version)' "$log_path"; then
  printf 'FAIL: fixture load reported a bad game chunk or save read error\n'
  test_exit=1
fi

if [[ "$test_exit" == 0 ]] && grep -q 'SABER ROM CHECKS PASSED' "$log_path"; then
  printf 'PASS: Saber ROM checks\n'
  result_exit=0
else
  printf 'FAIL: Saber ROM checks (exit=%s)\n' "$test_exit"
  result_exit="$test_exit"
  if [[ "$result_exit" == 0 ]]; then
    result_exit=1
  fi
fi
printf 'log: %s\n' "$log_path"
rm -rf "$run_dir"
exit "$result_exit"
