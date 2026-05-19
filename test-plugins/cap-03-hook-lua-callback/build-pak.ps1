# Build kcdx_test_cap_three.pak for CAP-03's pak Lua side.

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$luac = "C:\Program Files (x86)\Lua\5.1\luac.exe"
if (Test-Path $luac) {
    & $luac -p scripts\mods\kcdx_test_cap_three.lua
    if ($LASTEXITCODE -ne 0) { throw "Lua syntax error" }
    Write-Output "  syntax: OK"
}

$staging = Join-Path $env:TEMP "kcdx_test_cap_three_staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory $staging -Force | Out-Null
Copy-Item scripts $staging\scripts -Recurse

$out = Join-Path $PSScriptRoot "build\kcdx_test_cap_three"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory $out\Data -Force | Out-Null
Copy-Item mod.manifest $out\mod.manifest

$pak = Join-Path $out\Data "kcdx_test_cap_three.pak"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $staging, $pak,
    [System.IO.Compression.CompressionLevel]::NoCompression, $false)

Write-Output "  built: $pak ($(((Get-Item $pak).Length)) bytes)"
Write-Output "  drop folder: $out -> <game>/mods/"
