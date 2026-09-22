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
  [string]$RuntimeBinDir = 'C:\msys64\mingw64\bin',
  [ValidateSet('SDL3', 'SDL2')][string]$SdlBackend = 'SDL3'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'MegaManXSNESRecomp.exe'
$assets = Join-Path $build 'assets'
$mods = Join-Path $build 'mods'

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
if (Test-Path -LiteralPath $mods) {
  Copy-Item -LiteralPath $mods -Destination $stage -Recurse
}

# keybinds.ini is user-generated (PSR-style rebind UI) and only exists next
# to the exe once someone has actually rebound a key; ship it if present.
$kb = Join-Path $build 'keybinds.ini'
if (Test-Path -LiteralPath $kb) {
  Copy-Item -LiteralPath $kb -Destination $stage
}

# Runtime DLLs are NOT enumerated by hand. A hardcoded list is a snapshot of
# the import graph on the day it was written, and it silently omits anything a
# later toolchain/SDL bump pulls in - e.g. SDL3 3.4.14 links libiconv-2.dll
# where 3.4.12 did not. The gap is invisible on a dev box (MSYS2 / Git for
# Windows put the DLL on PATH) and fatal on a player's machine: 0xC0000135 if
# it is simply absent, or 0xC000007B ("unable to start correctly") if the
# loader finds an unrelated 32-bit copy on PATH.
#
# Instead: stage the SDL backend, then walk the actual PE import graph and pull
# in the full transitive closure of non-OS dependencies. Anything unresolvable
# fails the release rather than shipping. Builds that link the MinGW runtime
# statically (SNESRECOMP_STATIC_RUNTIME, the default) correctly ship no
# libgcc/libstdc++/libwinpthread at all.
. (Join-Path $root 'snesrecomp\tools\release\RuntimeDllClosure.ps1')

$sdlDll = "$SdlBackend.dll"
$sdlSource = Join-Path $build $sdlDll
if (-not (Test-Path -LiteralPath $sdlSource)) {
  $sdlSource = Join-Path $RuntimeBinDir $sdlDll
}
if (-not (Test-Path -LiteralPath $sdlSource)) {
  throw "Required $SdlBackend runtime DLL missing from build or runtime bin: $sdlDll"
}
Copy-Item -LiteralPath $sdlSource -Destination $stage

$stagedDlls = Copy-RuntimeDllClosure -StageDir $stage -SearchDirs @($build, $RuntimeBinDir)
if ($stagedDlls.Count -gt 0) {
  Write-Host "Staged runtime dependency closure: $($stagedDlls -join ', ')"
}
# Re-read the stage rather than trusting the copy pass: every non-OS import
# must resolve inside the package, and every staged PE must be x64.
Assert-RuntimeDllClosure -StageDir $stage | Out-Null

$objdump = Join-Path $RuntimeBinDir 'objdump.exe'
if (-not (Test-Path -LiteralPath $objdump)) {
  $objdumpCmd = Get-Command 'objdump.exe' -ErrorAction SilentlyContinue
  if ($null -eq $objdumpCmd) {
    throw "objdump.exe not found; cannot verify release DLL dependencies"
  }
  $objdump = $objdumpCmd.Source
}

$windowsDlls = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
@(
  'ADVAPI32.dll', 'bcrypt.dll', 'COMCTL32.dll', 'comdlg32.dll',
  'CRYPT32.dll', 'd2d1.dll', 'dbghelp.dll', 'DWrite.dll', 'DWMAPI.dll', 'GDI32.dll', 'IMM32.dll',
  'IPHLPAPI.DLL', 'KERNEL32.dll', 'msvcrt.dll', 'ole32.dll',
  'OLEAUT32.dll', 'OPENGL32.dll', 'POWRPROF.dll', 'RPCRT4.dll',
  'SETUPAPI.dll', 'SHELL32.dll', 'SHLWAPI.dll', 'USER32.dll',
  'USP10.dll', 'UXTHEME.dll', 'VERSION.dll', 'WINMM.dll', 'WINTRUST.dll',
  'WS2_32.dll'
) | ForEach-Object { [void]$windowsDlls.Add($_) }

function Test-WindowsDll([string]$Name) {
  return $windowsDlls.Contains($Name) -or
    $Name.StartsWith('api-ms-win-', [StringComparison]::OrdinalIgnoreCase) -or
    $Name.StartsWith('ext-ms-win-', [StringComparison]::OrdinalIgnoreCase)
}

function Get-ImportedDlls([string]$Binary) {
  & $objdump -p $Binary | ForEach-Object {
    if ($_ -match 'DLL Name:\s*(\S+)') {
      $matches[1]
    }
  }
}

$dllSearchDirs = @($stage, $build, $RuntimeBinDir) |
  Where-Object { Test-Path -LiteralPath $_ } |
  ForEach-Object { [IO.Path]::GetFullPath($_) } |
  Select-Object -Unique

function Copy-DependencyDll([string]$Name) {
  $dest = Join-Path $stage $Name
  if (Test-Path -LiteralPath $dest) {
    return $dest
  }
  foreach ($dir in $dllSearchDirs) {
    $candidate = Join-Path $dir $Name
    if (Test-Path -LiteralPath $candidate) {
      Copy-Item -LiteralPath $candidate -Destination $stage
      Write-Host "Bundled dependency DLL: $Name"
      return $dest
    }
  }
  throw "Release dependency DLL not found in build/runtime dirs: $Name"
}

$pending = [System.Collections.Generic.Queue[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $stage -File |
  Where-Object { $_.Extension -in '.exe', '.dll' } |
  ForEach-Object { $pending.Enqueue($_.FullName) }

while ($pending.Count -gt 0) {
  $binary = $pending.Dequeue()
  $binaryFull = [IO.Path]::GetFullPath($binary)
  if (-not $seen.Add($binaryFull)) {
    continue
  }

  foreach ($dll in @(Get-ImportedDlls $binaryFull | Select-Object -Unique)) {
    if (Test-WindowsDll $dll) {
      continue
    }
    $depPath = Copy-DependencyDll $dll
    if ([IO.Path]::GetExtension($depPath).Equals(
        '.dll', [StringComparison]::OrdinalIgnoreCase)) {
      $pending.Enqueue($depPath)
    }
  }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ZIP entry names always use '/', regardless of the host OS. Compress-Archive
# preserves Windows backslashes, which POSIX extractors may treat as literal
# filename characters instead of directory separators.
$stagePrefix = $stageFull.TrimEnd('\') + '\'
$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse |
  Sort-Object FullName)
$archive = [IO.Compression.ZipFile]::Open(
  $zipFull, [IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($file in $files) {
    $fileFull = [IO.Path]::GetFullPath($file.FullName)
    if (-not $fileFull.StartsWith(
        $stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Refusing to archive a file outside the release stage: $fileFull"
    }
    $entryName = $fileFull.Substring($stagePrefix.Length).Replace('\', '/')
    if ($entryName.StartsWith('/') -or
        $entryName -match '(^|/)\.\.(/|$)') {
      throw "Unsafe ZIP entry name: $entryName"
    }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
      $archive, $fileFull, $entryName,
      [IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
} finally {
  $archive.Dispose()
}

$archive = [IO.Compression.ZipFile]::OpenRead($zipFull)
try {
  $badEntries = @($archive.Entries | Where-Object {
    $_.FullName.Contains('\') -or
    $_.FullName.StartsWith('/') -or
    $_.FullName -match '(^|/)\.\.(/|$)'
  })
  if ($badEntries.Count -ne 0) {
    throw "ZIP contains non-portable entry names: $(
      ($badEntries | ForEach-Object FullName) -join ', ')"
  }
  if ($archive.Entries.Count -ne $files.Count) {
    throw "ZIP entry count mismatch: expected $($files.Count), got $(
      $archive.Entries.Count)"
  }
} finally {
  $archive.Dispose()
}

Write-Host "--- $stageName ---"
Get-ChildItem -LiteralPath $stage | Select-Object Name, Length | Out-Host
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Out-Host
