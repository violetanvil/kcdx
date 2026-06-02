# PreToolUse(Write|Edit) — public/private boundary guard.
# WARN-ONLY: never blocks. Flags a Write/Edit to a PUBLIC-FACING file that
# introduces a reference to a PRIVATE document or the AI-development vocabulary.
# A public file ships to the public remote (publish-public.ps1 allowlist); it
# must not reference anything that stays private — the reference would be a
# broken link on public and a trace of how the repo is built.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }

    # Normalize to a repo-relative, forward-slash path.
    $repoRoot = (git rev-parse --show-toplevel 2>$null)
    if ($repoRoot) { $repoRoot = $repoRoot.Trim() }
    $rel = $path -replace '\\', '/'
    if ($repoRoot) {
        $rootFwd = ($repoRoot -replace '\\', '/').TrimEnd('/')
        if ($rel.StartsWith($rootFwd)) { $rel = $rel.Substring($rootFwd.Length).TrimStart('/') }
    }

    # PUBLIC-FACING = under an allowlisted public dir, or an allowlisted root file.
    # Keep in sync with publish-public.ps1's allowlist.
    $publicDirs  = @('src/','include/','vendor/','data/','examples/','kcdx-engine/','test-plugins/','tools/','docs/')
    $publicFiles = @('README.md','LICENSE','CMakeLists.txt','build.ps1','package-release.ps1')
    # Carve-outs: private subpaths inside an otherwise-public dir (internal
    # planning + bug trails under docs/). Keep in sync with publish-public.ps1
    # $PrivateSubpaths.
    # Trailing '/' = directory prefix; otherwise an exact-file carve-out.
    $privateSubpaths = @('docs/outstanding-work/','docs/known-issues/','docs/tech-debt/','docs/design.md','docs/design-gaps.md','docs/phase5c7b-plan.md','docs/VERIFY_PHASE2.md','docs/VERIFY_PHASE3.md','docs/VERIFY_PHASE4.md','docs/archive/','docs/phase5-rom-port-plan.md','docs/migration.md','examples/archive/','data/refdata-extractor/','data/seeds/')

    $isPublic = $false
    foreach ($d in $publicDirs)  { if ($rel -like "$d*") { $isPublic = $true; break } }
    if (-not $isPublic) { foreach ($f in $publicFiles) { if ($rel -eq $f) { $isPublic = $true; break } } }
    if ($isPublic) {
      foreach ($p in $privateSubpaths) {
        if ($p.EndsWith('/')) { if ($rel -like "$p*") { $isPublic = $false; break } }
        elseif ($rel -eq $p) { $isPublic = $false; break }
      }
    }
    if (-not $isPublic) { exit 0 }   # private file — may reference anything.

    # Post-operation content (Write supplies it; Edit applies the replacement).
    $content = $null
    if ($data.tool_input.content) {
        $content = $data.tool_input.content
    } elseif ($data.tool_input.old_string) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }
        $current = [System.IO.File]::ReadAllText($path)
        $old = $data.tool_input.old_string
        $new = $data.tool_input.new_string
        if ([bool]$data.tool_input.replace_all) {
            $content = $current.Replace($old, $new)
        } else {
            $idx = $current.IndexOf($old)
            if ($idx -lt 0) { exit 0 }
            $content = $current.Substring(0, $idx) + $new + $current.Substring($idx + $old.Length)
        }
    }
    if (-not $content) { exit 0 }

    # --- Forbidden references in a public file -------------------------------
    # 1) Literal private paths (these dirs/files never reach public).
    # 2) AI-development vocabulary (would betray how the repo is built).
    $patterns = [ordered]@{
        '.claude/ path reference'   = '\.claude/'
        'CLAUDE.md reference'       = 'CLAUDE\.md'
        '_research/ path reference' = '_research/'
        'third-party-ghidra/ ref'   = 'third-party-ghidra/'
        'test-fixtures/ reference'  = 'test-fixtures/'
        'publish-public.ps1 ref'    = 'publish-public\.ps1'
        'Claude / Anthropic'        = '(?i)\b(claude|anthropic)\b'
        'subagent / orchestrator'   = '(?i)\b(subagent|orchestrator)\b'
        'AP-rule citation'          = '\bAP\d{1,2}\b'
        'skill invocation'          = '(?<![A-Za-z0-9_])/(execute|feature|debug|commit|code-review|verification-checkpoint|research-disassembly|governance-architect|senior-architect-(consult|reply)|step-review|architect-review)\b'
        'PROBE-naming scheme'       = '\bPROBE [A-Z](\.\d+)*\b'
        'dev-phase scheme'          = '\bPhase \d+[a-z]?(\.\d+)*\b'
        'dev-subphase scheme'       = '\bsub-\d+[a-z]?\b'
        'FIX-naming scheme'         = '\bFIX [A-Z]\b'
        # Bare private-rule .md filename (cited without the .claude/rules/ path).
        # These name a private governance file → broken link + build-trace on public.
# NOTE: loader-architecture.md is EXCLUDED — it collides with the public
        # doc docs/loader-architecture.md (a bare ref is ambiguous; the .claude/
        # path form is still caught above). All others are private-only basenames.
        'private rule-file name'    = '\b(cornerstones|anti-patterns|skse-parity|toml-schema|hook-engine|concurrency-git|results-driven|address-library|lua-bridge|lua-api-surface|naming-namespaces|docs-discipline|deletion-hygiene|lua-precision|lua-callback-threading|reverse-engineering|pak-mods|test-suite|skeptical-expert|public-private-boundary|fail-state-logging|anti-pattern-rationale)\.md\b'
    }

    $hits = New-Object System.Collections.Generic.List[string]
    $lines = $content -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($label in $patterns.Keys) {
            if ($lines[$i] -match $patterns[$label]) {
                $snippet = $lines[$i].Trim()
                if ($snippet.Length -gt 100) { $snippet = $snippet.Substring(0, 100) + '...' }
                $hits.Add(("  line {0}: {1} -> {2}" -f ($i + 1), $label, $snippet))
            }
        }
    }

    if ($hits.Count -gt 0) {
        $msg  = "Public/private boundary WARN: $rel is a PUBLIC-facing file but references private material or AI-development vocabulary. "
        $msg += "Public files ship to the public remote and must reference NONE of: .claude/, CLAUDE.md, _research/, third-party-ghidra/, test-fixtures/, publish-public.ps1, or the words Claude/Anthropic/subagent/orchestrator/AP<n>/skill-slash-commands. "
        $msg += "Rewrite to state the fact directly without the private citation. Findings:`n" + ($hits -join "`n")
        [Console]::Error.WriteLine($msg)
    }
    exit 0
} catch {
    exit 0
}
