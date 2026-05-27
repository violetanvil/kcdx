<#
.SYNOPSIS
  Game-agnostic launcher for EnumerateFunctions.py -- runs the read-only Ghidra
  headless function-inventory dump over one or more analyzed programs in a
  project and writes one <module>.functions.csv per program.

.DESCRIPTION
  The reusable front door for the parallel-ghidra-research enumeration step
  (docs/outstanding-work/parallel-ghidra-research.md). Nothing game-specific is
  baked in: project path, program list, version tag, and output dir are all
  parameters, so the same script serves KCD2 re-analysis on a new game build AND
  a future port to another game's binaries.

  Read-only: passes -readOnly -noanalysis to analyzeHeadless, so the project DB
  is never mutated. Safe to run against a shared analyzed project.

.PARAMETER ProjectDir
  Directory containing the .gpr / .rep (e.g. third-party-ghidra/ghidra_project).

.PARAMETER ProjectName
  Ghidra project name (the .gpr basename, e.g. KCD2).

.PARAMETER OutDir
  Directory the per-module CSVs are written into. Created if absent.

.PARAMETER Modules
  One or more program names already imported into the project (e.g. WHGame.dll).
  Omit to enumerate EVERY program in the project.

.PARAMETER VersionTag
  Optional game-version label stamped into each CSV row's game_version column.

.PARAMETER GhidraDir
  Ghidra install root. Defaults to the sibling ghidra_12.1_PUBLIC.

.EXAMPLE
  ./enumerate-functions.ps1 -ProjectDir ../ghidra_project -ProjectName KCD2 `
      -OutDir ../../_research/parallel-ghidra-research/inventory `
      -Modules WHGame.dll,BugSplat64.dll -VersionTag release_1_5_1164953_841

.EXAMPLE
  # Every program in the project, no version tag:
  ./enumerate-functions.ps1 -ProjectDir ../ghidra_project -ProjectName KCD2 `
      -OutDir ../../_research/parallel-ghidra-research/inventory
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]   $ProjectDir,
    [Parameter(Mandatory = $true)] [string]   $ProjectName,
    [Parameter(Mandatory = $true)] [string]   $OutDir,
    [Parameter(Mandatory = $false)][string[]] $Modules,
    [Parameter(Mandatory = $false)][string]   $VersionTag = "",
    [Parameter(Mandatory = $false)][string]   $GhidraDir
)

$ErrorActionPreference = "Stop"
$scriptRoot = $PSScriptRoot

if (-not $GhidraDir) {
    $GhidraDir = Join-Path (Split-Path $scriptRoot -Parent) "ghidra_12.1_PUBLIC"
}

$headless = Join-Path $GhidraDir "support/analyzeHeadless.bat"
if (-not (Test-Path $headless)) {
    throw "analyzeHeadless.bat not found at: $headless  (pass -GhidraDir to override)"
}
if (-not (Test-Path (Join-Path $ProjectDir "$ProjectName.rep"))) {
    throw "Ghidra project '$ProjectName' not found in: $ProjectDir"
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}
# Resolve to an absolute path -- the postScript writes here from Ghidra's CWD,
# not ours, so a relative OutDir would land in the wrong place.
$OutDirAbs = (Resolve-Path $OutDir).Path

# -postScript args: out_dir then optional version_tag (matches EnumerateFunctions.py).
$scriptArgs = @($OutDirAbs)
if ($VersionTag) { $scriptArgs += $VersionTag }

# Common headless args. -process <Module> is added per-module below; with no
# -Modules we run a single headless pass over the whole project (-process *).
$common = @(
    $ProjectDir, $ProjectName,
    "-scriptPath", $scriptRoot,
    "-postScript", "EnumerateFunctions.java"
) + $scriptArgs + @("-noanalysis", "-readOnly")

# analyzeHeadless.bat -> launch.bat ends with a `pause` that fires only when
# DOUBLE_CLICKED=y AND exit != 0. In a non-interactive shell that pause has no
# stdin and DEADLOCKS the run forever. Two defenses: clear DOUBLE_CLICKED, and
# pipe EOF into the .bat so any stray pause returns immediately instead of
# blocking. (Without this, a *failed* run hangs rather than reporting its error.)
$env:DOUBLE_CLICKED = ""

function Invoke-Enumerate([string[]]$processArgs, [string]$label) {
    Write-Host "==> enumerating $label" -ForegroundColor Cyan
    # Empty stdin via the pipeline guarantees `pause` can't block on a tty read.
    $null | & $headless @common @processArgs
    if ($LASTEXITCODE -ne 0) {
        throw "analyzeHeadless failed for $label (exit $LASTEXITCODE) -- see $env:APPDATA\ghidra\*\application.log"
    }
}

if ($Modules) {
    foreach ($m in $Modules) {
        Invoke-Enumerate @("-process", $m) $m
    }
} else {
    # No module filter -> every program in the project.
    Invoke-Enumerate @("-process", "*") "all programs"
}

Write-Host "`nDone. CSVs in: $OutDirAbs" -ForegroundColor Green
Get-ChildItem -Path $OutDirAbs -Filter "*.functions.csv" |
    Select-Object Name, Length | Format-Table -AutoSize
