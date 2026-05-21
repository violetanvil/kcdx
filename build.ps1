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

# Verify all three expected outputs exist.
$dll      = Join-Path $buildDir "Release\kcdx.dll"
$exe      = Join-Path $buildDir "Release\kcdx.exe"
$watchdog = Join-Path $buildDir "Release\kcdx-watchdog.exe"

foreach ($pair in @(@($dll, "engine DLL"), @($exe, "launcher exe"), @($watchdog, "watchdog"))) {
    $path = $pair[0]; $label = $pair[1]
    if (-not (Test-Path $path)) { throw "Expected $label not found at $path" }
    $size = (Get-Item $path).Length
    Write-Host ("Built: {0,-60} {1,8:N1} KB ({2})" -f $path, ($size/1KB), $label) -ForegroundColor Green
}

# Stale .asi from before the Phase 1 rename
$asi = Join-Path $buildDir "Release\kcdx.asi"
if (Test-Path $asi) {
    Remove-Item $asi -Force
    Write-Host "Removed stale: $asi (Phase 1: kcdx.asi -> kcdx.dll)" -ForegroundColor DarkYellow
}
