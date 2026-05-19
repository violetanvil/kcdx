# Build the kcdx_test_paklua.pak (zip of scripts/) and assemble the
# game-folder mod layout:
#
#   build/kcdx_test_paklua/
#     mod.manifest
#     Data/
#       kcdx_test_paklua.pak    <- zip of ./scripts/
#
# Drop build/kcdx_test_paklua/ into <game>/mods/.

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# 1. Syntax-check the Lua source first (catches the silent-discard
#    failure mode where CryEngine drops a pak script on parse error).
$luac = "C:\Program Files (x86)\Lua\5.1\luac.exe"
if (Test-Path $luac) {
    & $luac -p scripts\mods\kcdx_test_paklua.lua
    if ($LASTEXITCODE -ne 0) { throw "Lua syntax error" }
    Write-Output "  syntax: OK"
}

# 2. Build pak (zip with no compression, .pak extension).
$staging = Join-Path $env:TEMP "kcdx_test_paklua_staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory $staging -Force | Out-Null
Copy-Item scripts $staging\scripts -Recurse

$out = Join-Path $PSScriptRoot "build\kcdx_test_paklua"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory $out\Data -Force | Out-Null
Copy-Item mod.manifest $out\mod.manifest

$pak = Join-Path $out\Data "kcdx_test_paklua.pak"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $staging, $pak,
    [System.IO.Compression.CompressionLevel]::NoCompression, $false)

Write-Output "  built: $pak ($(((Get-Item $pak).Length)) bytes)"
Write-Output "  drop folder: $out -> <game>/mods/"
