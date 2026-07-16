<#
make_release.ps1 — build the MMX windows release zip.

Ships ONE windows zip (and ONLY a zip — never a bare exe; mmx.exe is
useless without SDL2.dll). Stage layout matches every release since
v1.0.7: mmx.exe (Production|x64, console-free), SDL2.dll, config.ini
(repo root copy), keybinds.ini (if present next to the built exe),
README.md (repo README verbatim).

Zip lands in release-stage\MegaManXSNESRecomp-windows-<Version>.zip.
Publish via gh AFTER the user signs off:

  gh release create v<Version> release-stage\MegaManXSNESRecomp-windows-<Version>.zip

Usage: powershell -File tools\make_release.ps1 -Version 1.0.8
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin-x64-Production'
$out = Join-Path $root 'release-stage'
New-Item -ItemType Directory -Force $out | Out-Null

# SnesRecompBuildVersion stamps SNESRECOMP_BUILD_VERSION into the exe so
# user crash reports (last_run_report.json / crash_report_*.json) name the
# release they came from.
& $MSBuild (Join-Path $root 'mmx.sln') /p:Configuration=Production /p:Platform=x64 "/p:SnesRecompBuildVersion=$Version" /m /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }

$stageName = "MegaManXSNESRecomp-windows-$Version"
$stage = Join-Path $out $stageName
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin 'mmx.exe') $stage
Copy-Item (Join-Path $bin 'SDL2.dll') $stage
Copy-Item (Join-Path $root 'config.ini') $stage
$kb = Join-Path $bin 'keybinds.ini'
if (Test-Path $kb) { Copy-Item $kb $stage }
Copy-Item (Join-Path $root 'README.md') $stage

# Launcher assets (Dear ImGui launcher_ng) - fonts + images the GUI loads from
# assets/ next to the exe (SDL_GetBasePath). Replaces the old RmlUi launcher/ dir.
$assetsSrc = Join-Path $bin 'assets'
if (-not (Test-Path $assetsSrc)) { throw "assets/ missing at $assetsSrc - did the Production build stage the launcher_ng assets?" }
Copy-Item $assetsSrc $stage -Recurse

$zip = Join-Path $out "$stageName.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

# Archive the PDB NEXT TO the zip (never inside it): it's what turns a
# user's crash_minidump_*.dmp / module+offset stack into file:line. Keep
# it with the release artifacts forever.
$pdb = Join-Path $bin 'mmx.pdb'
if (Test-Path $pdb) {
  Copy-Item $pdb (Join-Path $out "mmx-$Version.pdb")
  Write-Host "PDB archived: $out\mmx-$Version.pdb (do NOT ship; keep for symbolizing user crash dumps)"
} else {
  Write-Warning "mmx.pdb missing from $bin - crash minidumps from this release won't symbolize."
}
Write-Host "--- $stageName ---"
Get-ChildItem $stage | Select-Object Name, Length | Out-Host
Get-Item $zip | Select-Object Name, Length | Out-Host
