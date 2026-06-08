# build.ps1 — Windows native build via Visual Studio 2022
$ErrorActionPreference = "Stop"

$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# NASM assembles the safetyhook mid-hook stub (NASM-syntax .asm). Put it on PATH
# for this build process only (no global machine change) so the VS generator's
# ASM_NASM detection finds it. Checks PATH first, then the default winget/NASM
# install locations. A clone-and-go contributor installs NASM (e.g.
# `winget install NASM.NASM`); the build finds it here.
if (-not (Get-Command nasm -ErrorAction SilentlyContinue)) {
    $nasmCandidates = @(
        (Join-Path $env:LOCALAPPDATA "bin\NASM"),
        "C:\Program Files\NASM",
        "C:\Program Files (x86)\NASM"
    )
    $nasmDir = $nasmCandidates | Where-Object { Test-Path (Join-Path $_ "nasm.exe") } | Select-Object -First 1
    if ($nasmDir) {
        $env:PATH = "$nasmDir;$env:PATH"
        Write-Host "Using NASM at $nasmDir" -ForegroundColor DarkCyan
    } else {
        throw "NASM not found (needed to assemble the safetyhook mid-hook stub). Install it: winget install NASM.NASM"
    }
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

# Stale .asi from before the kcdx.asi -> kcdx.dll rename
$asi = Join-Path $buildDir "Release\kcdx.asi"
if (Test-Path $asi) {
    Remove-Item $asi -Force
    Write-Host "Removed stale: $asi (kcdx.asi -> kcdx.dll)" -ForegroundColor DarkYellow
}
