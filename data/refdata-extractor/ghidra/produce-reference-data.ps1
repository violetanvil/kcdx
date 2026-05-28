<#
.SYNOPSIS
  Launch the production reference-data extractor (the Ghidra side: functions/ +
  statements/ + referenced_vars/ + call_edges/) headless + read-only over a
  Ghidra program.

.DESCRIPTION
  Wraps analyzeHeadless with the deadlock defenses that cost time on the first
  run (recorded so they are never removed):
    - launch.bat ends with a `pause` that fires on non-zero exit when
      DOUBLE_CLICKED=y; in a non-interactive shell it DEADLOCKS forever. We
      defend with $env:DOUBLE_CLICKED="" + piping EOF.
    - project paths MUST be absolute (a '.'-relative -ProjectDir aborts).
    - analyzeHeadless.bat DROPS an empty-string positional argument, which
      shifts every later positional arg. So NO empty cell may precede the range
      args: an empty -VersionTag is passed as the sentinel "__none__" (Java
      normalizes it back to ""), and an absent -Limit is passed as the literal
      -1 (the Java "all functions" sentinel).

  SCOPE: defaults to the FULL BINARY (all functions). Pass -Limit N for a quick
  sample, or -RvaStart/-RvaEnd (paired) for a bounded RVA range (resume / a
  parallel worker over a disjoint range).

.PARAMETER ProjectDir   Absolute path to the Ghidra project dir.
.PARAMETER ProjectName  The project name (e.g. KCD2).
.PARAMETER OutDir       The dump root (table dirs created under it).
.PARAMETER Module       The program to process (e.g. WHGame.dll).
.PARAMETER VersionTag   Game-version label stamped into game_version. Default "".
.PARAMETER Limit        Max functions within the range (quick sample). 0/absent = all.
.PARAMETER RvaStart     Inclusive RVA-range start (hex, e.g. 0x100000). Paired with -RvaEnd.
.PARAMETER RvaEnd       EXCLUSIVE RVA-range end (hex). Paired with -RvaStart.

.EXAMPLE
  # full binary
  ./produce-reference-data.ps1 -ProjectDir <abs>/ghidra_project -ProjectName KCD2 `
      -OutDir <abs>/refdata-full -Module WHGame.dll -VersionTag release_1_5_1164953_841

.EXAMPLE
  # a bounded range worker (resume / parallel)
  ./produce-reference-data.ps1 -ProjectDir <abs>/ghidra_project -ProjectName KCD2 `
      -OutDir <abs>/refdata-w0 -Module WHGame.dll -RvaStart 0x0 -RvaEnd 0x100000
#>
param(
    [Parameter(Mandatory = $true)] [string] $ProjectDir,
    [Parameter(Mandatory = $true)] [string] $ProjectName,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [Parameter(Mandatory = $true)] [string] $Module,
    [Parameter(Mandatory = $false)][string] $VersionTag = "",
    [Parameter(Mandatory = $false)][int]    $Limit = 0,
    [Parameter(Mandatory = $false)][string] $RvaStart = "",
    [Parameter(Mandatory = $false)][string] $RvaEnd = ""
)

$ErrorActionPreference = "Stop"

# Range params are paired-or-error (mirrors the Java-side check).
if (($RvaStart -ne "") -ne ($RvaEnd -ne "")) {
    throw "Pass -RvaStart and -RvaEnd together (an open-ended worker range is ambiguous)."
}

# Resolve to absolute (Ghidra rejects '.'-relative project paths).
$ProjectDirAbs = (Resolve-Path $ProjectDir).Path
$OutDirAbs = [System.IO.Path]::GetFullPath($OutDir)
$ScriptRoot = $PSScriptRoot                      # tools/refdata-extractor/ghidra
$Blake3Root = Join-Path $ScriptRoot "blake3"     # the Apache Blake3 package root

# The Ghidra install lives in the (gitignored) third-party-ghidra/ tree.
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..\..")).Path
$Headless = Join-Path $RepoRoot "third-party-ghidra\ghidra_12.1_PUBLIC\support\analyzeHeadless.bat"

# Build the positional script args. NO empty cell may precede the range args
# (analyzeHeadless.bat drops empty strings): empty version -> "__none__",
# absent limit -> "-1".
$verArg = if ($VersionTag -eq "") { "__none__" } else { $VersionTag }
$limArg = if ($Limit -le 0) { "-1" } else { "$Limit" }
$scriptArgs = @($OutDirAbs, $verArg, $limArg)
if ($RvaStart -ne "") {
    $scriptArgs += $RvaStart
    $scriptArgs += $RvaEnd
}

Write-Host "==> producing reference data for $Module"
if ($RvaStart -ne "") { Write-Host "    (range $RvaStart .. $RvaEnd)" }
elseif ($Limit -gt 0) { Write-Host "    (SAMPLE limit=$Limit)" }
else { Write-Host "    (FULL BINARY -- this is a long run)" }

# Deadlock defenses: neutralize DOUBLE_CLICKED + pipe EOF so the trailing pause
# never blocks on stdin in a non-interactive shell.
$env:DOUBLE_CLICKED = ""

$ghidraArgs = @(
    $ProjectDirAbs, $ProjectName,
    "-process", $Module,
    "-noanalysis", "-readOnly",
    "-scriptPath", "$ScriptRoot;$Blake3Root",
    "-postScript", "ProduceReferenceData.java"
) + $scriptArgs

$null | & $Headless @ghidraArgs
if ($LASTEXITCODE -ne 0) {
    throw "analyzeHeadless failed for $Module (exit $LASTEXITCODE) -- see %APPDATA%\ghidra\*\application.log"
}

Write-Host "Done. Output in: $OutDirAbs"
Get-ChildItem $OutDirAbs -ErrorAction SilentlyContinue | Select-Object Name
