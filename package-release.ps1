# package-release.ps1 — build the engine-only release zip
#
# Produces release-staging/kcdx-<version>.zip with the explicit
# allowlist of files that ship to end users:
#   - dinput8.dll                  (Ultimate-ASI-Loader, downloaded fresh)
#   - dinput8.dll.LICENSE.txt      (its MIT license)
#   - plugins/kcdx.asi             (this project's build output)
#
# Examples in the repo, source code, vendor sources, etc. are NOT included.
# Plugins ship from their own repos.
#
# Usage:
#   pwsh ./package-release.ps1                # uses version "dev"
#   pwsh ./package-release.ps1 -Version 0.1.0
#
# Prereqs: build.ps1 must have already produced build/Release/kcdx.asi.

[CmdletBinding()]
param(
    [string]$Version = "dev",
    [string]$UlahVersion = "v9.7.1"
)
$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$Asi = Join-Path $RepoRoot "build/Release/kcdx.asi"
$Staging = Join-Path $RepoRoot "release-staging"
$Download = Join-Path $RepoRoot "_download"

if (-not (Test-Path $Asi)) {
    throw "kcdx.asi not found at $Asi — run build.ps1 first"
}

Write-Host "Cleaning release-staging..." -ForegroundColor Cyan
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Path "$Staging/plugins" | Out-Null

if (-not (Test-Path $Download)) {
    New-Item -ItemType Directory -Path $Download | Out-Null
}

# Download Ultimate-ASI-Loader x64 if not already cached
$UlahZip = Join-Path $Download "ulah_$UlahVersion.zip"
if (-not (Test-Path $UlahZip)) {
    $url = "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/$UlahVersion/Ultimate-ASI-Loader_x64.zip"
    Write-Host "Downloading $url..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $UlahZip
}

# Extract just dinput8.dll
Write-Host "Extracting dinput8.dll..." -ForegroundColor Cyan
Expand-Archive -Path $UlahZip -DestinationPath $Staging -Force

# Fetch ULAH license to satisfy MIT redistribution
Write-Host "Fetching Ultimate-ASI-Loader license..." -ForegroundColor Cyan
$ulahLic = Invoke-WebRequest -Uri "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/master/license" -UseBasicParsing
$licHeader = @"
This LICENSE applies ONLY to dinput8.dll (Ultimate-ASI-Loader by ThirteenAG).
The rest of this distribution (kcdx.asi etc.) is MIT-licensed; see the
kcdx repository at https://github.com/violetanvil/kcdx for its license.

"@
Set-Content -Path "$Staging/dinput8.dll.LICENSE.txt" -Value ($licHeader + $ulahLic.Content) -NoNewline

# Copy kcdx.asi
Write-Host "Copying kcdx.asi..." -ForegroundColor Cyan
Copy-Item $Asi "$Staging/plugins/kcdx.asi"

# Build the zip
$ZipName = "kcdx-$Version.zip"
$ZipPath = Join-Path $RepoRoot "release-staging/$ZipName"
Write-Host "Creating $ZipName..." -ForegroundColor Cyan
if (Test-Path $ZipPath) { Remove-Item $ZipPath }

# Explicit allowlist of files to include
$Include = @(
    "$Staging/dinput8.dll",
    "$Staging/dinput8.dll.LICENSE.txt",
    "$Staging/plugins/kcdx.asi"
)
foreach ($f in $Include) {
    if (-not (Test-Path $f)) { throw "Missing required file: $f" }
}

# Compress with the right relative paths by working from $Staging
Push-Location $Staging
try {
    Compress-Archive `
        -Path "dinput8.dll", "dinput8.dll.LICENSE.txt", "plugins" `
        -DestinationPath $ZipPath `
        -CompressionLevel Optimal
}
finally {
    Pop-Location
}

# Verify the zip contents match the allowlist (defense against accidental drift)
$ExpectedEntries = @(
    "dinput8.dll",
    "dinput8.dll.LICENSE.txt",
    "plugins/kcdx.asi"
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
    $actual = $zip.Entries | ForEach-Object { $_.FullName.Replace("\","/") } | Where-Object { -not $_.EndsWith("/") }
    $extra = $actual | Where-Object { $ExpectedEntries -notcontains $_ }
    $missing = $ExpectedEntries | Where-Object { $actual -notcontains $_ }
    if ($extra) { throw "Release zip has unexpected entries: $($extra -join ', ')" }
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
