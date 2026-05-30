# PreToolUse(Bash|PowerShell) — public-leak / remote-identity guard
# (.claude/rules/concurrency-git.md §"Remotes"; CLAUDE.md publish-public rule).
# The private tree is TRACKED, not gitignored — a hand `git push public` ships
# .claude/, _research/, CLAUDE.md, the whole AI-dev trace. Irreversible once
# pushed. Public is reached ONLY via publish-public.ps1 (allowlist projection).
#
# BLOCK: `git push public ...`, `git push --all`, `git push --mirror` — each ships
#        the comprehensive tree to a remote it must never reach by hand.
# WARN:  `git remote add|set-url`, `git config user.email|user.name` — silent
#        identity/remote drift that a later push could leak through.
# Untouched: bare `git push` (-> private), publish-public.ps1, push to origin/private.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $cmd = $data.tool_input.command
    if (-not $cmd) { exit 0 }

    $cp = '(?:^|[;&|]|&&|\|\|)\s*'
    # Only inspect actual `git push` invocations at command position.
    $isPush = $cmd -match "(?i)${cp}git\s+push\b"

    if ($isPush) {
        # --all / --mirror push the comprehensive ref set regardless of remote.
        if ($cmd -match '(?i)\s--(?:all|mirror)\b') {
            [Console]::Error.WriteLine("Push-target BLOCK: `git push --all` / `--mirror` ships EVERY ref of the comprehensive private tree (.claude/, _research/, CLAUDE.md — all TRACKED, not gitignored) and can reach the public remote. The private->public boundary is the publish-public.ps1 allowlist, NEVER a hand push (.claude/rules/concurrency-git.md §Remotes). Push to private explicitly (bare `git push`), or run publish-public.ps1 for a sanitized public snapshot.")
            exit 2
        }
        # An explicit `public` remote arg. Isolate the push's own segment so a
        # `public` inside a later chained command / quoted message can't false-trip.
        $seg = $cmd
        $m = [regex]::Match($cmd, "(?i)${cp}git\s+push\b[^\n;&|]*")
        if ($m.Success) { $seg = $m.Value }
        if ($seg -match '(?i)\bpush\b[^\n]*\bpublic\b') {
            [Console]::Error.WriteLine("Push-target BLOCK: a hand `git push public` ships the comprehensive private tree (.claude/, _research/, CLAUDE.md, the AI-dev trace — TRACKED, not gitignored) to the PUBLIC remote. This is a one-way, irreversible leak. The public remote is updated ONLY by publish-public.ps1, which projects an explicit allowlist (.claude/rules/concurrency-git.md §Remotes; CLAUDE.md). Run that script for a public update; never push public by hand.")
            exit 2
        }
        exit 0
    }

    # Remote / identity mutation — WARN (silent drift a future push could leak).
    $warnRules = @(
        @{ rx = "(?i)${cp}git\s+remote\s+(?:add|set-url)\b";        why = "`git remote add/set-url` changes where a push goes — a mis-set remote could route a later push to public (.claude/rules/concurrency-git.md §Remotes)" }
        @{ rx = "(?i)${cp}git\s+config\s+(?:--\w+\s+)*user\.(?:email|name)\b"; why = "`git config user.email/name` changes commit identity — drift here puts the wrong author on commits (workspace suppresses attribution by policy)" }
    )
    foreach ($r in $warnRules) {
        if ($cmd -match $r.rx) {
            [Console]::Error.WriteLine("Remote/identity WARN: $($r.why). Confirm this is intended; the private/public split and commit identity are project invariants. This does not block.")
            exit 0
        }
    }
    exit 0
} catch {
    exit 0
}
