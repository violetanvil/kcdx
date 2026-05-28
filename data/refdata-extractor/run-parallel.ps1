<#
.SYNOPSIS
  Parallel orchestrator for the production reference-data extractor: split the
  WHGame.dll function set into N disjoint RVA ranges (balanced by FUNCTION COUNT),
  run one worker per range over its OWN copy of the Ghidra project, then merge the
  per-worker outputs into one final out-dir.

.DESCRIPTION
  WHY parallel + per-worker project copies (both load-bearing, probe-verified):
    - Ghidra locks a project EXCLUSIVELY -- N headless jobs against ONE project
      fail to attach (the 2nd openProject throws). So each worker runs against its
      OWN copy of third-party-ghidra/ghidra_project/, made under a scratch root
      and deleted after. (See docs/outstanding-work/parallel-ghidra-research.md
      Section 8: exclusive lock -> per-worker copies.)
    - Ranges are balanced by FUNCTION COUNT, not even RVA width: function density
      is uneven across RVA, so an even-width split makes stragglers.

  WHY shard-boundary snapping (the merge-correctness invariant):
    The extractor's ShardWriter shards on shardOf(rva)=rva//0x100000 and names
    each shard <table>_<startRva:08x>.csv. If a worker range boundary fell
    mid-shard, two adjacent workers would each write the SAME shard filename for
    different RVAs in that 1 MiB window -- a merge collision. So every computed
    boundary is snapped DOWN to a 0x100000 multiple. The ranges are half-open
    [start,end) and PARTITION the whole span (worker i end == worker i+1 start),
    so each shard file is produced by EXACTLY ONE worker and the merge is a pure
    file COLLECT (no per-row dedup); a shard-filename collision across workers is
    a partition bug and FAILS loudly.

  Each worker produces a COMPLETE 5-table / 6-dir output for its slice:
    - the Java launcher (produce-reference-data.ps1 -RvaStart/-RvaEnd) ->
      functions/ statements/ referenced_vars/ call_edges/
    - produce_signatures.py (range) -> signatures/
    - produce_caller_reg_args.py (range) -> caller_reg_args/

.PARAMETER ProjectDir   Source Ghidra project dir (copied per worker). Default: third-party-ghidra/ghidra_project.
.PARAMETER ProjectName  Ghidra project name. Default: KCD2.
.PARAMETER Module       Program to process. Default: WHGame.dll.
.PARAMETER OutDir       FINAL merged out-dir (table dirs created under it). REQUIRED.
.PARAMETER Workers      Worker count / range count. Default 8.
.PARAMETER VersionTag   game_version label. Default release_1_5_1164953_841.
.PARAMETER EnumCsv      Function-inventory CSV (rva column) used for the count split.
.PARAMETER WhgameDll    WHGame.dll path for the two Python passes.
.PARAMETER ScratchRoot  Root for per-worker project copies + per-worker out-dirs.
                        Default: a unique dir under $env:TEMP. Created if absent.
.PARAMETER RvaStart     TEST hook: explicit span start (hex). Paired with -RvaEnd.
.PARAMETER RvaEnd       TEST hook: explicit span end (hex, EXCLUSIVE). Paired with -RvaStart.
                        When given, the split covers ONLY [RvaStart,RvaEnd) (used by
                        the 2-worker verification over a small bounded span).
.PARAMETER ExplicitBounds  TEST hook: a comma-separated list of >=2 hex boundaries
                        (e.g. "0xff800,0x100000,0x100800") used VERBATIM as the
                        worker partition, bypassing the CSV count-split + snapping.
                        Lets the verification drive a cheap, shard-crossing 2-worker
                        run over a bounded span. NOT for production (the snapping
                        invariant is the whole point of the default split); the
                        boundaries must already be chosen so worker shards stay
                        disjoint. Each adjacent pair becomes one [start,end) range.
.PARAMETER DryRun       Compute + print the N ranges (from the CSV, snapped) and
                        EXIT -- no project copy, no worker launch, no merge.

.EXAMPLE
  # REAL full 8-way dump (the user runs this)
  pwsh ./tools/refdata-extractor/run-parallel.ps1 `
      -OutDir E:\refdata-full -Workers 8 -VersionTag release_1_5_1164953_841 `
      -WhgameDll "E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

.EXAMPLE
  # inspect the 8 full-binary ranges cheaply
  pwsh ./tools/refdata-extractor/run-parallel.ps1 -OutDir nul -DryRun
#>
[CmdletBinding()]
param(
    [string] $ProjectDir,
    [string] $ProjectName = "KCD2",
    [string] $Module      = "WHGame.dll",
    [Parameter(Mandatory = $true)][string] $OutDir,
    [int]    $Workers     = 8,
    [string] $VersionTag  = "release_1_5_1164953_841",
    [string] $EnumCsv,
    [string] $WhgameDll,
    [string] $ScratchRoot,
    [string] $RvaStart    = "",
    [string] $RvaEnd      = "",
    [string] $ExplicitBounds = "",
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$SHARD_SPAN = 0x100000

# ---------------------------------------------------------------------------
# Path resolution (this script lives at tools/refdata-extractor/run-parallel.ps1).
# ---------------------------------------------------------------------------
$ScriptRoot = $PSScriptRoot
$RepoRoot   = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$Launcher   = Join-Path $ScriptRoot "ghidra\produce-reference-data.ps1"
$PyDir      = Join-Path $ScriptRoot "python"
$SigPy      = Join-Path $PyDir "produce_signatures.py"
$CallerPy   = Join-Path $PyDir "produce_caller_reg_args.py"

if (-not $ProjectDir) { $ProjectDir = Join-Path $RepoRoot "third-party-ghidra\ghidra_project" }
if (-not $EnumCsv)    { $EnumCsv = Join-Path $RepoRoot "_research\parallel-ghidra-research\inventory\WHGame.dll.functions.csv" }
if (-not $WhgameDll)  {
    $WhgameDll = "E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
}

if (-not (Test-Path $EnumCsv))  { throw "enumeration CSV not found: $EnumCsv" }
if ($Workers -lt 1) { throw "-Workers must be >= 1 (got $Workers)." }
if (($RvaStart -ne "") -ne ($RvaEnd -ne "")) {
    throw "Pass -RvaStart and -RvaEnd together (a half-open test span [start,end))."
}

# ---------------------------------------------------------------------------
# Range split, balanced by FUNCTION COUNT, snapped to shard boundaries.
#
# Read every rva from the CSV (within the optional test span), sort ascending.
# For boundary i in 1..N-1: the ideal cut is the function at index
# round(i*total/N); take that function's rva and snap it DOWN to a SHARD_SPAN
# multiple. The whole span [spanStart, spanEnd) is partitioned: worker 0 starts
# at spanStart (snapped down), worker N-1 ends at spanEnd (snapped UP so the last
# function's shard is fully inside). Snapping can collapse two boundaries onto
# the same shard; we drop duplicate/non-monotonic boundaries so every emitted
# range is non-empty and they still partition with no gap/overlap (this can yield
# fewer than N ranges only on a pathologically dense span -- acceptable; never an
# overlap or gap).
# ---------------------------------------------------------------------------
function Get-Ranges {
    param([string]$Csv, [int]$N, [long]$SpanStart, [long]$SpanEnd, [bool]$HasSpan)

    # Read rva column. (Header: module,game_version,rva,...)
    $rvas = [System.Collections.Generic.List[long]]::new()
    $reader = [System.IO.StreamReader]::new($Csv)
    try {
        $null = $reader.ReadLine()  # header
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.Length -eq 0) { continue }
            # rva is the 3rd comma-separated field; no quoting in this column.
            $c1 = $line.IndexOf(',')
            if ($c1 -lt 0) { continue }
            $c2 = $line.IndexOf(',', $c1 + 1)
            if ($c2 -lt 0) { continue }
            $c3 = $line.IndexOf(',', $c2 + 1)
            $rvaStr = if ($c3 -lt 0) { $line.Substring($c2 + 1) } else { $line.Substring($c2 + 1, $c3 - $c2 - 1) }
            $rvaStr = $rvaStr.Trim()
            if (-not $rvaStr.StartsWith("0x")) { continue }
            $rva = [Convert]::ToInt64($rvaStr.Substring(2), 16)
            if ($HasSpan -and -not ($rva -ge $SpanStart -and $rva -lt $SpanEnd)) { continue }
            $rvas.Add($rva)
        }
    } finally { $reader.Dispose() }

    if ($rvas.Count -eq 0) { throw "no functions found in CSV within the requested span." }
    $rvas.Sort()
    $total = $rvas.Count
    $minRva = $rvas[0]
    $maxRva = $rvas[$total - 1]

    # span start/end (shard-aligned). Test span overrides; else derive from data.
    $start0 = if ($HasSpan) { $SpanStart } else { $minRva }
    $endN   = if ($HasSpan) { $SpanEnd }   else { $maxRva + 1 }
    $start0Snap = [long]([math]::Floor([double]$start0 / $SHARD_SPAN)) * $SHARD_SPAN
    # End snapped UP to the next shard boundary (so the last function's whole shard
    # belongs to the last worker). A span end already on a boundary stays put.
    $endNSnap = [long]([math]::Ceiling([double]$endN / $SHARD_SPAN)) * $SHARD_SPAN

    # Interior boundaries by function-count cut, snapped down.
    $bounds = [System.Collections.Generic.List[long]]::new()
    $bounds.Add($start0Snap)
    for ($i = 1; $i -lt $N; $i++) {
        $idx = [int][math]::Round([double]$i * $total / $N)
        if ($idx -ge $total) { $idx = $total - 1 }
        if ($idx -lt 0) { $idx = 0 }
        $b = [long]([math]::Floor([double]$rvas[$idx] / $SHARD_SPAN)) * $SHARD_SPAN
        # keep strictly increasing + inside (start0Snap, endNSnap).
        if ($b -gt $bounds[$bounds.Count - 1] -and $b -lt $endNSnap) {
            $bounds.Add($b)
        }
    }
    $bounds.Add($endNSnap)

    # Build [start,end) ranges + per-range function counts.
    $ranges = @()
    for ($i = 0; $i -lt $bounds.Count - 1; $i++) {
        $s = $bounds[$i]; $e = $bounds[$i + 1]
        $cnt = 0
        foreach ($r in $rvas) { if ($r -ge $s -and $r -lt $e) { $cnt++ } }
        $ranges += [pscustomobject]@{
            Index    = $i
            Start    = $s
            End      = $e
            StartHex = ("0x{0:x}" -f $s)
            EndHex   = ("0x{0:x}" -f $e)
            FnCount  = $cnt
        }
    }
    return [pscustomobject]@{ Ranges = $ranges; Total = $total; MinRva = $minRva; MaxRva = $maxRva }
}

# Count functions in [s,e) from the CSV rva column (report + accounting).
function Get-FnCount {
    param([string]$Csv, [long]$S, [long]$E)
    $n = 0
    $reader = [System.IO.StreamReader]::new($Csv)
    try {
        $null = $reader.ReadLine()
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.Length -eq 0) { continue }
            $c1 = $line.IndexOf(','); if ($c1 -lt 0) { continue }
            $c2 = $line.IndexOf(',', $c1 + 1); if ($c2 -lt 0) { continue }
            $c3 = $line.IndexOf(',', $c2 + 1)
            $rvaStr = if ($c3 -lt 0) { $line.Substring($c2 + 1) } else { $line.Substring($c2 + 1, $c3 - $c2 - 1) }
            $rvaStr = $rvaStr.Trim()
            if (-not $rvaStr.StartsWith("0x")) { continue }
            $rva = [Convert]::ToInt64($rvaStr.Substring(2), 16)
            if ($rva -ge $S -and $rva -lt $E) { $n++ }
        }
    } finally { $reader.Dispose() }
    return $n
}

if ($ExplicitBounds -ne "") {
    # TEST path: use the given boundaries verbatim (no CSV split, no snapping).
    $bvals = @($ExplicitBounds.Split(',') | ForEach-Object { [Convert]::ToInt64($_.Trim().Substring(2), 16) })
    if ($bvals.Count -lt 2) { throw "-ExplicitBounds needs >=2 boundaries." }
    $rngs = @()
    $tot = 0
    for ($i = 0; $i -lt $bvals.Count - 1; $i++) {
        $s = $bvals[$i]; $e = $bvals[$i + 1]
        if ($s -ge $e) { throw "boundaries must strictly increase ($($bvals[$i]) >= $($bvals[$i+1]))." }
        $cnt = Get-FnCount -Csv $EnumCsv -S $s -E $e
        $tot += $cnt
        $rngs += [pscustomobject]@{ Index=$i; Start=$s; End=$e; StartHex=("0x{0:x}" -f $s); EndHex=("0x{0:x}" -f $e); FnCount=$cnt }
    }
    $split = [pscustomobject]@{ Ranges=$rngs; Total=$tot; MinRva=$bvals[0]; MaxRva=$bvals[$bvals.Count-1] }
} else {
    $hasSpan = ($RvaStart -ne "")
    $spanStart = if ($hasSpan) { [Convert]::ToInt64($RvaStart.Substring(2), 16) } else { 0 }
    $spanEnd   = if ($hasSpan) { [Convert]::ToInt64($RvaEnd.Substring(2), 16) }   else { 0 }
    $split = Get-Ranges -Csv $EnumCsv -N $Workers -SpanStart $spanStart -SpanEnd $spanEnd -HasSpan $hasSpan
}
$ranges = $split.Ranges

# ---------------------------------------------------------------------------
# Print + validate the partition (gap/overlap/alignment). Always shown.
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host ("Reference-data parallel split: {0} function(s), {1} range(s)" -f $split.Total, $ranges.Count)
Write-Host ("  data RVA span [0x{0:x} .. 0x{1:x}]" -f $split.MinRva, $split.MaxRva)
Write-Host ("  {0,-3} {1,-14} {2,-14} {3}" -f "#", "start", "end", "fn_count")
$prevEnd = $null
$partitionOk = $true
foreach ($r in $ranges) {
    Write-Host ("  {0,-3} {1,-14} {2,-14} {3}" -f $r.Index, $r.StartHex, $r.EndHex, $r.FnCount)
    # Shard-disjointness is decided by the INTERIOR boundaries (where one worker
    # ends and the next begins). Each interior boundary == worker i end == worker
    # i+1 start MUST be a SHARD_SPAN multiple, else the same shard file is owned by
    # two workers. The outer edges (first start, last end) only bound the data
    # span; they may sit mid-shard harmlessly (the count-split snaps them too, but
    # the -ExplicitBounds test path intentionally bounds a sub-shard span).
    if ($r.Index -gt 0 -and ($r.Start % $SHARD_SPAN) -ne 0) {
        Write-Host ("    !! interior boundary {0} not shard-aligned -- shards would collide" -f $r.StartHex)
        $partitionOk = $false
    }
    if ($null -ne $prevEnd -and $r.Start -ne $prevEnd) {
        Write-Host ("    !! GAP/OVERLAP: range {0} start {1} != previous end 0x{2:x}" -f $r.Index, $r.StartHex, $prevEnd)
        $partitionOk = $false
    }
    if ($r.Start -ge $r.End) { Write-Host ("    !! empty/inverted range {0}" -f $r.Index); $partitionOk = $false }
    $prevEnd = $r.End
}
$sumFn = ($ranges | Measure-Object -Property FnCount -Sum).Sum
Write-Host ("  sum of per-range fn_count = {0} (CSV total in span = {1})" -f $sumFn, $split.Total)
if ($sumFn -ne $split.Total) { Write-Host "    !! per-range counts do not sum to total"; $partitionOk = $false }
if (-not $partitionOk) { throw "range partition is invalid (see !! above) -- refusing to launch." }
Write-Host "  partition OK: contiguous, interior boundaries shard-aligned, counts sum to total."
Write-Host ""

if ($DryRun) { Write-Host "(-DryRun) range math only; no copy/launch/merge."; return }

# ---------------------------------------------------------------------------
# Resolve the rest of the inputs only when actually running.
# ---------------------------------------------------------------------------
if (-not (Test-Path $Launcher))  { throw "launcher not found: $Launcher" }
if (-not (Test-Path $SigPy))     { throw "produce_signatures.py not found: $SigPy" }
if (-not (Test-Path $CallerPy))  { throw "produce_caller_reg_args.py not found: $CallerPy" }
if (-not (Test-Path $ProjectDir)){ throw "Ghidra project dir not found: $ProjectDir" }
if (-not (Test-Path $WhgameDll)) { throw "WHGame.dll not found: $WhgameDll" }

if (-not $ScratchRoot) {
    $ScratchRoot = Join-Path $env:TEMP ("kcdx-refdata-parallel-{0}" -f ([guid]::NewGuid().ToString('N').Substring(0,8)))
}
New-Item -ItemType Directory -Force -Path $ScratchRoot | Out-Null
$ScratchRoot = (Resolve-Path $ScratchRoot).Path
$OutDirAbs = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDirAbs | Out-Null

Write-Host "Scratch root: $ScratchRoot"
Write-Host "Final out-dir: $OutDirAbs"

$projSrc = (Resolve-Path $ProjectDir).Path
$createdCopies = @()

try {
    # -------- launch all workers in parallel (each: copy project, Java, Python) --------
    $jobs = @()
    foreach ($r in $ranges) {
        $wCopy = Join-Path $ScratchRoot ("proj-w{0}" -f $r.Index)
        $wOut  = Join-Path $ScratchRoot ("out-w{0}"  -f $r.Index)
        $createdCopies += $wCopy

        $jobs += Start-Job -Name ("refdata-w{0}" -f $r.Index) -ScriptBlock {
            param($idx, $startHex, $endHex, $projSrc, $wCopy, $wOut,
                  $launcher, $sigPy, $callerPy, $projName, $module, $verTag, $whgame, $enumCsv)

            $ErrorActionPreference = "Stop"
            function Log($m) { Write-Output ("[w{0}] {1}" -f $idx, $m) }

            Log "copying project -> $wCopy"
            Copy-Item -Path $projSrc -Destination $wCopy -Recurse -Force
            # The copied project dir name may differ; the launcher takes the dir.
            $copiedDir = $wCopy

            New-Item -ItemType Directory -Force -Path $wOut | Out-Null

            Log "Java extractor range $startHex..$endHex"
            & pwsh -NoProfile -File $launcher `
                -ProjectDir $copiedDir -ProjectName $projName `
                -OutDir $wOut -Module $module -VersionTag $verTag `
                -RvaStart $startHex -RvaEnd $endHex
            if ($LASTEXITCODE -ne 0) { throw "[w$idx] Java extractor failed (exit $LASTEXITCODE)" }

            Log "produce_signatures range $startHex..$endHex"
            & python $sigPy $whgame $enumCsv $wOut "0" "2000" $startHex $endHex
            if ($LASTEXITCODE -ne 0) { throw "[w$idx] produce_signatures failed (exit $LASTEXITCODE)" }

            Log "produce_caller_reg_args range $startHex..$endHex"
            & python $callerPy $whgame $enumCsv $wOut "0" $startHex $endHex
            if ($LASTEXITCODE -ne 0) { throw "[w$idx] produce_caller_reg_args failed (exit $LASTEXITCODE)" }

            Log "DONE -> $wOut"
            return $wOut
        } -ArgumentList $r.Index, $r.StartHex, $r.EndHex, $projSrc, $wCopy, $wOut, `
            $Launcher, $SigPy, $CallerPy, $ProjectName, $Module, $VersionTag, $WhgameDll, $EnumCsv
    }

    Write-Host ("Launched {0} worker job(s); waiting..." -f $jobs.Count)
    $jobs | Wait-Job | Out-Null

    $workerOuts = @()
    $anyFailed = $false
    foreach ($j in $jobs) {
        Receive-Job $j | ForEach-Object {
            if ($_ -is [string] -and (Test-Path $_)) { $workerOuts += $_ } else { Write-Host $_ }
        }
        if ($j.State -ne 'Completed') { Write-Host ("Worker {0} state: {1}" -f $j.Name, $j.State); $anyFailed = $true }
    }
    $jobs | Remove-Job -Force
    if ($anyFailed) { throw "one or more workers failed -- see output above. NOT merging." }
    if ($workerOuts.Count -ne $ranges.Count) {
        throw ("worker out-dir count {0} != range count {1}" -f $workerOuts.Count, $ranges.Count)
    }

    # -------- MERGE: collect shard files; FAIL on any cross-worker collision --------
    Write-Host ""
    Write-Host "Merging worker outputs into $OutDirAbs"
    $tables = @("functions", "statements", "referenced_vars", "call_edges", "signatures", "caller_reg_args")
    foreach ($table in $tables) {
        $destTable = Join-Path $OutDirAbs $table
        New-Item -ItemType Directory -Force -Path $destTable | Out-Null
        $seen = @{}   # shard-filename -> owning worker out-dir (collision detector)
        foreach ($wOut in $workerOuts) {
            $srcTable = Join-Path $wOut $table
            if (-not (Test-Path $srcTable)) { continue }
            foreach ($f in (Get-ChildItem $srcTable -Filter "*.csv" -File)) {
                if ($seen.ContainsKey($f.Name)) {
                    throw ("SHARD COLLISION: {0}/{1} produced by BOTH {2} and {3} -- partition bug." `
                        -f $table, $f.Name, $seen[$f.Name], $wOut)
                }
                $seen[$f.Name] = $wOut
                Copy-Item -Path $f.FullName -Destination (Join-Path $destTable $f.Name) -Force
            }
        }
        Write-Host ("  {0,-18} {1} shard file(s)" -f $table, $seen.Count)
    }
    Write-Host ""
    Write-Host "MERGE OK -- no shard-filename collisions across workers."
    Write-Host "Final merged output: $OutDirAbs"
}
finally {
    # Per-worker copies + out-dirs are disposable -- always clean the scratch root.
    if (Test-Path $ScratchRoot) {
        Write-Host "Cleaning scratch root: $ScratchRoot"
        Remove-Item -Recurse -Force $ScratchRoot -ErrorAction SilentlyContinue
    }
}
