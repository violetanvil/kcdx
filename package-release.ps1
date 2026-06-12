# package-release.ps1 — build the engine-only release zip
#
# Produces release-staging/kcdx-<version>.zip with the explicit
# allowlist of files that ship to end users. v0.2 layout:
#
#   <zip-root>/
#   ├── kcdx.exe                                (launcher; the only file at game-bin root)
#   ├── kcdx-README.txt                         (install + Steam launch options)
#   ├── engine/
#   │   ├── kcdx.dll                            (engine; injected by kcdx.exe)
#   │   ├── kcdx-watchdog.exe                   (crash-bundle sidecar)
#   │   ├── load_order.toml                     (shipped defaults; user-editable)
#   │   └── builtin/
#   │       └── <fix-name>/
#   │           ├── kcdx.toml
#   │           └── <fix>.dll                    (optional; for DLL-based engine fixes)
#   └── plugins/                                (empty in the zip — user-installed only)
#
# Notes vs the v0.1 layout:
#   - dinput8.dll (Ultimate ASI Loader) is GONE. No longer required;
#     kcdx.exe injects kcdx.dll directly via CreateRemoteThread+LoadLibrary.
#   - No more kcdx.asi extension. Engine is kcdx.dll under engine/.
#   - Engine-owned files (kcdx.dll, watchdog, engine.toml, builtin/) all
#     live under engine/, NOT in plugins/.
#   - plugins/ is exclusively for third-party user-installed plugins.
#
# Third-party plugins ship from their own repos. Engine-fix plugins are
# part of kcdx itself and live under engine/builtin/<name>/ — added by
# hand to $EngineBuiltin below as new fixes land.
#
# Usage:
#   pwsh ./package-release.ps1                 # uses version "dev"
#   pwsh ./package-release.ps1 -Version 0.2.0
#
# Prereqs: build.ps1 must have already produced:
#   build/Release/kcdx.exe          (launcher)
#   build/Release/kcdx.dll          (engine)
#   build/Release/kcdx-watchdog.exe (crash-bundle helper)

[CmdletBinding()]
param(
    [string]$Version = "dev"
)
$ErrorActionPreference = "Stop"

$RepoRoot      = $PSScriptRoot
$LauncherExe   = Join-Path $RepoRoot "build/Release/kcdx.exe"
$EngineDll     = Join-Path $RepoRoot "build/Release/kcdx.dll"
$Watchdog      = Join-Path $RepoRoot "build/Release/kcdx-watchdog.exe"
$EngineBuiltin = Join-Path $RepoRoot "kcdx-engine/builtin"
$Staging       = Join-Path $RepoRoot "release-staging"

foreach ($pair in @(@($LauncherExe, "kcdx.exe (launcher)"),
                    @($EngineDll,   "kcdx.dll (engine)"),
                    @($Watchdog,    "kcdx-watchdog.exe"))) {
    $path = $pair[0]; $label = $pair[1]
    if (-not (Test-Path $path)) { throw "$label not found at $path — run build.ps1 first" }
}

Write-Host "Cleaning release-staging..." -ForegroundColor Cyan
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Path "$Staging/kcdx-engine" | Out-Null
New-Item -ItemType Directory -Path "$Staging/kcdx-plugins" | Out-Null

# Launcher + README at the zip root.
Write-Host "Copying kcdx.exe + kcdx-README.txt..." -ForegroundColor Cyan
Copy-Item $LauncherExe "$Staging/kcdx.exe"
$readmePath = Join-Path $RepoRoot "kcdx-README.txt"
if (Test-Path $readmePath) {
    Copy-Item $readmePath "$Staging/kcdx-README.txt"
} else {
    # Minimal placeholder so users have install instructions in the zip.
    Set-Content -Path "$Staging/kcdx-README.txt" -Value @"
kcdx — KCD2 mod extender

Install:
  1. Extract this zip into <game>\Bin\Win64MasterMasterSteamPGO\ so that:
       kcdx.exe        sits next to KingdomCome.exe
       kcdx-engine/    is a sibling folder (auto-created on first run)
       kcdx-plugins/   is a sibling folder (for user-installed plugins)
  2. In Steam: right-click Kingdom Come: Deliverance II → Properties →
     Launch Options → set to the full path of kcdx.exe (quoted).
  3. Launch via Steam as usual. kcdx.exe injects kcdx.dll on launch.

If kcdx doesn't load, check kcdx-engine/logs/kcdx-launcher_<timestamp>.log
for the diagnostic trail.

See https://github.com/violetanvil/kcdx for full docs.
"@
}

# Engine binaries under kcdx-engine/.
Write-Host "Copying kcdx-engine/kcdx.dll + kcdx-engine/kcdx-watchdog.exe..." -ForegroundColor Cyan
Copy-Item $EngineDll  "$Staging/kcdx-engine/kcdx.dll"
Copy-Item $Watchdog   "$Staging/kcdx-engine/kcdx-watchdog.exe"

# Engine builtins (kcdx-engine/builtin/<fix>/). Source is in the repo
# under kcdx-engine/builtin/; ships at the same path in the zip.
Write-Host "Copying kcdx-engine/builtin/..." -ForegroundColor Cyan
$BuiltinFixes = @()
if (Test-Path $EngineBuiltin) {
    New-Item -ItemType Directory -Path "$Staging/kcdx-engine/builtin" -Force | Out-Null
    Get-ChildItem -Path $EngineBuiltin -Directory | ForEach-Object {
        $srcToml = Join-Path $_.FullName "kcdx.toml"
        if (Test-Path $srcToml) {
            $dstDir = "$Staging/kcdx-engine/builtin/$($_.Name)"
            New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
            Copy-Item $srcToml "$dstDir/kcdx.toml"
            $BuiltinFixes += "kcdx-engine/builtin/$($_.Name)/kcdx.toml"
            # Pick up the optional <fix>.dll if present.
            Get-ChildItem -Path $_.FullName -Filter "*.dll" | ForEach-Object {
                Copy-Item $_.FullName "$dstDir/$($_.Name)"
                $BuiltinFixes += "kcdx-engine/builtin/$(Split-Path -Leaf $_.DirectoryName)/$($_.Name)"
            }
            Write-Host "  + $($_.Name)" -ForegroundColor Cyan
        }
    }
} else {
    Write-Host "  (no kcdx-engine/builtin/ in repo; skipping)" -ForegroundColor Yellow
}

# Shipped-defaults load_order.toml. Source: kcdx-engine/load_order.toml.
$LoadOrderSrc = Join-Path $RepoRoot "kcdx-engine/load_order.toml"
if (Test-Path $LoadOrderSrc) {
    Write-Host "Copying kcdx-engine/load_order.toml (shipped defaults)..." -ForegroundColor Cyan
    Copy-Item $LoadOrderSrc "$Staging/kcdx-engine/load_order.toml"
}

# Engine behavior catalog (data/behavior-catalog/*.lua + README.md). Engine-owned
# data asset: the engine reads it at runtime from <kcdx-engine>/behavior-catalog/,
# so it ships under kcdx-engine/ in the zip (source lives at data/behavior-catalog/).
$CatalogSrc = Join-Path $RepoRoot "data/behavior-catalog"
$CatalogEntries = @()
if (Test-Path $CatalogSrc) {
    Write-Host "Copying kcdx-engine/behavior-catalog/..." -ForegroundColor Cyan
    $CatalogDst = "$Staging/kcdx-engine/behavior-catalog"
    New-Item -ItemType Directory -Path $CatalogDst -Force | Out-Null
    Get-ChildItem -Path $CatalogSrc -File | ForEach-Object {
        Copy-Item $_.FullName "$CatalogDst/$($_.Name)"
        $CatalogEntries += "kcdx-engine/behavior-catalog/$($_.Name)"
        Write-Host "  + $($_.Name)" -ForegroundColor Cyan
    }
}

# Build the zip.
$ZipName = "kcdx-$Version.zip"
$ZipPath = Join-Path $RepoRoot "release-staging/$ZipName"
Write-Host "Creating $ZipName..." -ForegroundColor Cyan
if (Test-Path $ZipPath) { Remove-Item $ZipPath }

Push-Location $Staging
try {
    $CompressPaths = @("kcdx.exe", "kcdx-README.txt", "kcdx-engine", "kcdx-plugins")
    Compress-Archive `
        -Path $CompressPaths `
        -DestinationPath $ZipPath `
        -CompressionLevel Optimal
}
finally {
    Pop-Location
}

# Verify the zip's contents against the expected allowlist.
$ExpectedRoot = @(
    "kcdx.exe",
    "kcdx-README.txt",
    "kcdx-engine/kcdx.dll",
    "kcdx-engine/kcdx-watchdog.exe"
)
if (Test-Path "$Staging/kcdx-engine/load_order.toml") {
    $ExpectedRoot += "kcdx-engine/load_order.toml"
}
$ExpectedEntries = $ExpectedRoot + $BuiltinFixes + $CatalogEntries

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
    $actual = $zip.Entries `
        | ForEach-Object { $_.FullName.Replace("\","/") } `
        | Where-Object { -not $_.EndsWith("/") }
    $extra = $actual | Where-Object { $ExpectedEntries -notcontains $_ }
    $missing = $ExpectedEntries | Where-Object { $actual -notcontains $_ }
    if ($extra)   { throw "Release zip has unexpected entries: $($extra -join ', ')" }
    if ($missing) { throw "Release zip is missing entries: $($missing -join ', ')" }
}
finally {
    $zip.Dispose()
}

$size = (Get-Item $ZipPath).Length
Write-Host ""
Write-Host "OK: $ZipPath ($([math]::Round($size/1MB, 2)) MB)" -ForegroundColor Green
Write-Host "Contents (verified against allowlist):" -ForegroundColor Green
foreach ($e in $ExpectedEntries) { Write-Host "  $e" }
