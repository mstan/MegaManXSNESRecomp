[CmdletBinding()]
param(
    [string]$DirectRomPath,
    [string]$RuntimeBin = 'C:\msys64\mingw64\bin',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$rendererRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$rendererExe = Join-Path $rendererRoot 'build-custom\MegaManXSNESRecomp.exe'
$rendererData = Join-Path $rendererRoot 'build-custom\playtest'
if (-not (Test-Path -LiteralPath $rendererExe -PathType Leaf)) {
    throw "Build the custom renderer first: $rendererExe"
}
if (-not (Test-Path -LiteralPath $RuntimeBin -PathType Container)) {
    throw "Runtime DLL directory is missing: $RuntimeBin"
}
$rendererArguments = @('--config', 'config.ini')
if ($DirectRomPath) {
    $rendererArguments += (Resolve-Path -LiteralPath $DirectRomPath).Path
}
if ($CheckOnly) {
    Write-Output "Executable: $rendererExe"
    Write-Output "Settings and saves: $rendererData"
    Write-Output "Runtime DLLs: $RuntimeBin"
    return
}
[void](New-Item -ItemType Directory -Path $rendererData -Force)
Copy-Item -LiteralPath (Join-Path $rendererRoot 'build-custom\assets') `
    -Destination $rendererData -Recurse -Force
# Refresh package definitions while keeping this playtest's selections/saves.
Copy-Item -LiteralPath (Join-Path $rendererRoot 'mods\preloaded\packages') `
    -Destination (New-Item -ItemType Directory -Path (Join-Path $rendererData 'mods\preloaded') -Force).FullName `
    -Recurse -Force
$rendererState = Join-Path $rendererData 'mods\preloaded\state.toml'
if (-not (Test-Path -LiteralPath $rendererState)) {
    @'
format_version = 1
[[package]]
id = "megaman-x.enhancement.widescreen"
version = "1.0.0"
[[feature]]
package_id = "megaman-x.enhancement.widescreen"
id = "widescreen"
enabled = true
[feature.values]
renderer = "custom"
aspect = "adaptive"
hud = "edges"
'@ | Set-Content -LiteralPath $rendererState -Encoding ASCII
}
$rendererConfig = Join-Path $rendererData 'config.ini'
if (-not (Test-Path -LiteralPath $rendererConfig)) {
    Copy-Item -LiteralPath (Join-Path $rendererRoot 'config.ini') -Destination $rendererConfig
}
$rendererSavedPath = $env:PATH
Push-Location -LiteralPath $rendererData
try {
    $env:PATH = "$RuntimeBin;$rendererSavedPath"
    & $rendererExe @rendererArguments
    if ($LASTEXITCODE -ne 0) { throw "Renderer exited with $LASTEXITCODE; see $rendererData" }
} finally {
    $env:PATH = $rendererSavedPath
    Pop-Location
}
