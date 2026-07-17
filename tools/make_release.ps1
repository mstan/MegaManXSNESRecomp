<#
Package a completed Mega Man X Windows release build.

The build itself is intentionally separate so developers can choose their
toolchain and keep compilation priority under local control. The resulting zip
contains the executable, MinGW runtime dependencies, launcher assets,
configuration, and README. ROMs and ROM-derived generated C are never staged.

Ships ONE windows zip (and ONLY a zip — never a bare exe; the exe is useless
without its MinGW runtime DLLs). Zip lands in
release-stage\MegaManXSNESRecomp-windows-<Version>.zip. Publish via gh AFTER
the user signs off:

  gh release create v<Version> release-stage\MegaManXSNESRecomp-windows-<Version>.zip

Example:
  powershell -File tools\make_release.ps1 -Version 1.1.6 `
    -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin

NOTE: mingw builds produce no .pdb, so this script does not archive one.
Crash-dump symbolication for the mingw build is a follow-up (see
host_report crash capture work) — for now user crash reports name the
release via SNESRECOMP_BUILD_VERSION but can't be symbolized to file:line.
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = 'build-recompui',
  [string]$RuntimeBinDir = 'C:\msys64\mingw64\bin'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'MegaManXSNESRecomp.exe'
$assets = Join-Path $build 'assets'

if (-not (Test-Path -LiteralPath $exe)) {
  throw "Release executable missing: $exe"
}
if (-not (Test-Path -LiteralPath $assets)) {
  throw "recomp-ui launcher assets/ missing: $assets"
}

$out = Join-Path $root 'release-stage'
$stageName = "MegaManXSNESRecomp-windows-$Version"
$stage = Join-Path $out $stageName
$zip = Join-Path $out "$stageName.zip"

$outFull = [IO.Path]::GetFullPath($out).TrimEnd('\') + '\'
$stageFull = [IO.Path]::GetFullPath($stage)
$zipFull = [IO.Path]::GetFullPath($zip)
if (-not $stageFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase) -or
    -not $zipFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Refusing to clean release paths outside release-stage.'
}

if (Test-Path -LiteralPath $stage) {
  Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
  Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'config.ini') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
Copy-Item -LiteralPath $assets -Destination $stage -Recurse

# keybinds.ini is user-generated (PSR-style rebind UI) and only exists next
# to the exe once someone has actually rebound a key; ship it if present.
$kb = Join-Path $build 'keybinds.ini'
if (Test-Path -LiteralPath $kb) {
  Copy-Item -LiteralPath $kb -Destination $stage
}

$runtimeDlls = @(
  'SDL2.dll',
  'libgcc_s_seh-1.dll',
  'libstdc++-6.dll',
  'libwinpthread-1.dll'
)
foreach ($name in $runtimeDlls) {
  $source = Join-Path $RuntimeBinDir $name
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Required MinGW runtime DLL missing: $source"
  }
  Copy-Item -LiteralPath $source -Destination $stage
}

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host "--- $stageName ---"
Get-ChildItem -LiteralPath $stage | Select-Object Name, Length | Out-Host
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Out-Host
