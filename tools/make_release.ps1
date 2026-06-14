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

& $MSBuild (Join-Path $root 'mmx.sln') /p:Configuration=Production /p:Platform=x64 /m /v:quiet /nologo
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

# Launcher assets (RmlUi) - the GUI menu needs these next to the exe.
$launcherSrc = Join-Path $bin 'launcher'
if (-not (Test-Path $launcherSrc)) { throw "launcher/ assets missing at $launcherSrc - did the Production build run CopyLauncherAssets?" }
Copy-Item $launcherSrc $stage -Recurse

$zip = Join-Path $out "$stageName.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "--- $stageName ---"
Get-ChildItem $stage | Select-Object Name, Length | Out-Host
Get-Item $zip | Select-Object Name, Length | Out-Host
