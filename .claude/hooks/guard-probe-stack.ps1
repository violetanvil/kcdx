# PreToolUse(Write|Edit) — stacked-diagnostic-probe guard (.claude/rules/results-driven.md §Live-game-unknowns).
# WARN-ONLY: never blocks. Fires when an edit ADDS a `// === DIAGNOSTIC (PROBE …)`
# marker while another un-reverted probe marker already lives uncommitted in the
# working tree (git diff HEAD adds). That is the "stack PROBE B.2 on un-reverted
# PROBE B" shape /debug §2f forbids. Revert the prior probe before the next.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }
    if ($path -notmatch '\.(cpp|h|hpp|cc|cxx|inl)$') { exit 0 }

    $marker = '//\s*===\s*DIAGNOSTIC\s*\(PROBE'

    # Post-operation content (Write supplies it; Edit applies the replacement).
    $new_content = $null
    $old_content = ''
    if ($data.tool_input.content) {
        $new_content = $data.tool_input.content
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $old_content = [System.IO.File]::ReadAllText($path)
        }
    } elseif ($data.tool_input.old_string) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }
        $old_content = [System.IO.File]::ReadAllText($path)
        $old = $data.tool_input.old_string
        $new = $data.tool_input.new_string
        if ([bool]$data.tool_input.replace_all) {
            $new_content = $old_content.Replace($old, $new)
        } else {
            $idx = $old_content.IndexOf($old)
            if ($idx -lt 0) { exit 0 }
            $new_content = $old_content.Substring(0, $idx) + $new + $old_content.Substring($idx + $old.Length)
        }
    }
    if (-not $new_content) { exit 0 }

    # Does THIS edit add a probe marker? (more markers after than before)
    $new_n = ([regex]::Matches($new_content, $marker)).Count
    $old_n = ([regex]::Matches($old_content, $marker)).Count
    if ($new_n -le $old_n) { exit 0 }

    # Is there an un-reverted probe marker ALREADY in the working tree, in some
    # OTHER file? git diff HEAD added-lines carrying the marker, excluding $path.
    # No git / not a repo -> stay silent (warn-only, never error). Resolve the
    # repo root first so the `src` pathspec is anchored correctly.
    $fileDir = Split-Path -Parent $path
    $root = & git -C $fileDir rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $root) { exit 0 }
    $diff = & git -C $root diff HEAD -- 'src' 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $diff) { exit 0 }

    $editedLeaf = Split-Path -Leaf $path
    $curFile = $null
    $hasOtherProbe = $false
    foreach ($line in ($diff -split "`n")) {
        if ($line -match '^\+\+\+ b/(.+)$') { $curFile = $matches[1]; continue }
        if ($line -match '^\+' -and $line -notmatch '^\+\+\+' -and $line -match $marker) {
            if ($curFile -and (Split-Path -Leaf $curFile) -ne $editedLeaf) {
                $hasOtherProbe = $true
                break
            }
        }
    }

    if ($hasOtherProbe) {
        [Console]::Error.WriteLine("Probe-stack WARN: $editedLeaf adds a // === DIAGNOSTIC (PROBE ...) marker while another un-reverted probe site already exists uncommitted in src/. Stacking two live probes (e.g. PROBE B.2 on un-reverted PROBE B) confounds the next launch and violates one-variable-per-probe. Revert the prior probe before adding this one, unless you are explicitly building on it (.claude/rules/results-driven.md §Live-game-unknowns; debug/SKILL.md §2f). >2 probes or a hard bug -> switch to /debug for active-instrumentation tracking.")
    }
    exit 0
} catch {
    exit 0
}
