# PreToolUse(Write|Edit) — deletion-hygiene sweep guard (.claude/rules/deletion-hygiene.md).
# WARN-ONLY: never blocks. A per-write hook cannot grep the doc tree; it does what
# it can — fire a sweep reminder when an edit REMOVES a high-signal public-surface
# shape (count drops new-vs-old), so the survivor grep happens before the deletion
# lands. The review skills (step-review/code-review §2) carry the actual check.
# A line carrying `// approved: <reason>` (after explicit user sign-off) is exempt.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }
    # Runs on .cpp/.h/.toml/.md — where public-surface / TOML-table deletions land.
    if ($path -notmatch '\.(cpp|h|hpp|cc|cxx|inl|toml|md)$') { exit 0 }

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

    # Drop `// approved: <reason>` lines from both sides before counting.
    $filter_approved = {
        param($text)
        ($text -split "`n" | Where-Object { $_ -notmatch '//\s*approved:' }) -join "`n"
    }
    $new_filtered = & $filter_approved $new_content
    $old_filtered = & $filter_approved $old_content

    # ── Deletion hygiene — public surface removed, sweep for stale docs ────────
    # High-signal surface shapes: a TOML table, an exported entry point, a parser,
    # a kcdx.* registration. A drop in count means a surface was REMOVED.
    $surface_re = '(?m)^\s*(\[\[[a-z_]+\]\]|extern\s+"C"|.*\bkcdxPlugin_Load\b|.*\bParseOne[A-Z]\w*|.*\bkcdx\.[a-z]\w*\s*=)'
    $new_surfaces = [regex]::Matches($new_filtered, $surface_re).Count
    $old_surfaces = [regex]::Matches($old_filtered, $surface_re).Count
    if ($new_surfaces -lt $old_surfaces) {
        [Console]::Error.WriteLine("Deletion-hygiene WARN: $path removes a public surface (TOML table / exported entry point / parser / kcdx.* registration). Before this lands, grep docs/ + .claude/rules/ + CLAUDE.md for surviving PRESCRIPTIVE references to the removed token and fix them in the SAME commit (historical/comparative/superseded mentions are exempt). Per .claude/rules/deletion-hygiene.md.")
    }

    exit 0
} catch {
    exit 0
}
