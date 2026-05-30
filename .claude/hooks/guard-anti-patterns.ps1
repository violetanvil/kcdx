# PreToolUse(Write|Edit) — kcdx anti-pattern guard (.claude/rules/anti-patterns.md).
# WARN-ONLY: never blocks. Heuristic detectors for the mechanically-visible
# anti-patterns; the reviewer skills carry the full semantic check. A line
# carrying `// approved: <reason>` (after explicit user sign-off) is exempt.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }
    # C++ detectors (AP1/4/5/7) gate to C++ files individually; the deletion-
    # hygiene sweep also runs on .toml / .md (where TOML-table deletions land).
    $is_cpp = $path -match '\.(cpp|h|hpp|cc|cxx|inl)$'
    if (-not $is_cpp -and $path -notmatch '\.(toml|md)$') { exit 0 }

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

    # ── AP1 — raw RVA where an Address Library ID belongs ─────────────────────
    # Shape: a base/module pointer plus a 6+ hex-digit literal offset. The
    # Address Library source legitimately carries RVAs — the seed CSVs under
    # data/seeds/ and the address_library translation unit — skip those.
    if ($path -notmatch 'address_library' -and $path -notmatch '[\\/]seeds[\\/]' -and $path -notmatch '[\\/]rom_borrowed[\\/]') {
        $rva_re = '\b\w+\s*\+\s*0x[0-9a-fA-F]{6,}\b'
        $new_count = ([regex]::Matches($new_filtered, $rva_re)).Count
        $old_count = ([regex]::Matches($old_filtered, $rva_re)).Count
        if ($new_count -gt $old_count) {
            [Console]::Error.WriteLine("Anti-pattern WARN (AP1 raw RVA): $path adds a hardcoded base+offset literal. RVAs shift per KCD2 update — resolve by Address Library ID instead (add a row to the seed CSVs under data/seeds/; the reference DB regenerates from them — .claude/rules/address-library.md). Bypass: '// approved: <reason>' on the line after user sign-off.")
        }
    }

    # ── AP4 — MinHook used directly instead of through the engine ─────────────
    # Production hook installs go through hook_engine/conflict_engine, which call
    # MinHook internally. A raw MH_CreateHook outside those engines bypasses the
    # conflict engine. hook_engine.cpp / patch_engine.cpp own the real calls.
    if ($path -notmatch 'hook_engine' -and $path -notmatch 'patch_engine' -and $path -notmatch '[\\/]rom_borrowed[\\/]') {
        $mh_re = '\bMH_CreateHook(Ex|ApiEx|Api)?\b'
        $new_count = ([regex]::Matches($new_filtered, $mh_re)).Count
        $old_count = ([regex]::Matches($old_filtered, $mh_re)).Count
        if ($new_count -gt $old_count) {
            [Console]::Error.WriteLine("Anti-pattern WARN (AP4 hook outside the engine): $path calls MinHook directly. Production hooks register a conflict_engine footprint and apply via g_applyOrder — they don't call MH_* directly (.claude/rules/hook-engine.md). Bypass: '// approved: <reason>' after user sign-off.")
        }
    }

    # ── AP4 — ApplyAll() from production ──────────────────────────────────────
    # patch::ApplyAll / hook_engine::ApplyAll exist for the Lua-runtime + test
    # paths only. hooks.cpp production orchestration must walk g_applyOrder.
    if ($path -match '[\\/]hooks\.cpp$') {
        $applyall_re = '\b(patch|hook_engine)::ApplyAll\s*\('
        $new_count = ([regex]::Matches($new_filtered, $applyall_re)).Count
        $old_count = ([regex]::Matches($old_filtered, $applyall_re)).Count
        if ($new_count -gt $old_count) {
            [Console]::Error.WriteLine("Anti-pattern WARN (AP4 ApplyAll in production): $path adds an ApplyAll() call. Production orchestration walks conflict_engine::g_applyOrder (priority asc, name asc); ApplyAll() reintroduces the patches-always-before-hooks ordering bug (.claude/rules/hook-engine.md). Bypass: '// approved: <reason>' after user sign-off.")
        }
    }

    # ── AP5 — new kcdx-side Lua static-const sentinel ─────────────────────────
    # static const Node*/TValue* singletons trip WHGame's GC -> heap corruption.
    if ($is_cpp) {
    $sentinel_re = 'static\s+const\s+(Node|TValue)\b'
    $new_count = ([regex]::Matches($new_filtered, $sentinel_re)).Count
    $old_count = ([regex]::Matches($old_filtered, $sentinel_re)).Count
    if ($new_count -gt $old_count) {
        [Console]::Error.WriteLine("Anti-pattern WARN (AP5 Lua sentinel): $path adds a `static const Node/TValue`. Its address ends up in a GCObject on g->rootgc and trips WHGame's GC -> STATUS_HEAP_CORRUPTION (.claude/rules/lua-bridge.md). Use raw Lua C API / registry refs / userdata. Bypass: '// approved: <reason>' after user sign-off.")
    }
    }

    # ── AP7 — engine code edited; is its regression plugin moving with it? ─────
    # An absence violation: a single-file hook cannot see whether the matching
    # test-plugins/ plugin exists. So it does the one thing a per-write hook can —
    # fire a proactive reminder on every src/ | include/ touch (not test code,
    # not the test plugins themselves) so the test is built WITH the feature, not
    # after someone asks. Warn-only by design; the orchestrator (§A.4/§C step 2)
    # and step-review carry the actual gate.
    if ($path -match '(^|[\\/])(src|include)[\\/]' -and $path -notmatch '[\\/]test-plugins[\\/]') {
        [Console]::Error.WriteLine("Anti-pattern WARN (AP7 test coverage): $path is engine code. New capability or observable behavior change -> a test-plugins/ regression plugin ships in the SAME unit of work (build it now, don't wait to be asked). A behavior-changing bug fix -> a sub-test in that feature's existing plugin reproducing the bug. Pure internal refactor with no observable change -> no new test needed; say so. Per .claude/rules/test-suite.md (AP7).")
    }

    # ── Deletion hygiene — public surface removed, sweep for stale docs ────────
    # A per-write hook cannot grep the doc tree; it does what it can — fire a
    # sweep reminder when an edit REMOVES a high-signal public-surface shape, so
    # the survivor grep happens before the deletion lands. The review skills
    # (step-review/code-review §2) carry the actual check. Warn-only.
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
