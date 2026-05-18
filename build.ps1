# build.ps1 — Windows native build via Visual Studio 2022
$ErrorActionPreference = "Stop"

$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Write-Host "Configuring CMake..." -ForegroundColor Cyan
& cmake -S $PSScriptRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "Building Release..." -ForegroundColor Cyan
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$asi = Join-Path $buildDir "Release\kcdx.asi"
if (Test-Path $asi) {
    $size = (Get-Item $asi).Length
    Write-Host "Built: $asi ($([math]::Round($size/1KB, 1)) KB)" -ForegroundColor Green
} else {
    throw "Expected output kcdx.asi not found at $asi"
}
