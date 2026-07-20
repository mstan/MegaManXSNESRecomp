# play.ps1 — one-click: regen (static-coverage --cfg-roots) + non-trace build + launch.
#
# regen uses tools/regen.sh (wired for --cfg-roots: every declared func is
# materialized as AOT, interpreter is the failsafe floor). The build is a
# non-trace Release (build-play, TRACE=OFF) so audio is representative — the
# build-trace configuration runs below realtime and crackles due to
# instrumentation overhead.
#
# Usage:
#   tools/play.ps1                # regen (usa) + build + launch
#   tools/play.ps1 -SkipRegen     # skip regen (reuse current src/gen)
#   tools/play.ps1 -NoRun         # build only, print exe path, do not launch
param([switch]$NoRun, [switch]$SkipRegen)
$ErrorActionPreference = 'Stop'

$repo  = Split-Path $PSScriptRoot -Parent
$mingw = 'C:\msys64\mingw64\bin'
$exeName = 'MegaManXSNESRecomp.exe'
$rom     = Join-Path $repo 'mmx.sfc'

$env:PATH = "$mingw;$env:PATH"
$env:SNESRECOMP_ANALYSIS_BACKEND = 'native'
Set-Location $repo

Get-Process ($exeName -replace '\.exe$','') -ErrorAction SilentlyContinue | Stop-Process -Force

if (-not $SkipRegen) {
    Write-Host "=== regen (usa, --cfg-roots) ===" -ForegroundColor Cyan
    & "$mingw\bash.exe" tools/regen.sh usa --no-tests
    if ($LASTEXITCODE -ne 0) { throw "regen failed" }
}

$bd = Join-Path $repo 'build-play'
if (-not (Test-Path (Join-Path $bd 'CMakeCache.txt'))) {
    Write-Host "=== configure build-play (non-trace) ===" -ForegroundColor Cyan
    cmake -G Ninja -B $bd -S $repo `
        -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_ENABLE_TRACE=OFF `
        -DCMAKE_C_COMPILER="$mingw/gcc.exe" `
        -DCMAKE_CXX_COMPILER="$mingw/g++.exe" `
        -DCMAKE_MAKE_PROGRAM="$mingw/ninja.exe"
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}

Write-Host "=== build ===" -ForegroundColor Cyan
cmake --build $bd -j 4
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$exe = Join-Path $bd $exeName
Write-Host "Built: $exe" -ForegroundColor Green
if ($NoRun) { return }
Write-Host "=== launch ===" -ForegroundColor Cyan
Start-Process $exe -ArgumentList $rom -WorkingDirectory $bd
