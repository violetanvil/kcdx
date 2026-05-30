# PreToolUse(Bash|PowerShell) — force-push auto-safener
# (.claude/rules/concurrency-git.md §"Destructive ops" — network-irreversible tier).
# AUTO-SAFEN (not block, not warn): silently rewrites a bare `git push --force`
# / `-f` into `--force-with-lease`, which REFUSES if the remote moved under you —
# exactly the parallel-chat collision case. A non-colliding force-push still
# succeeds, so flow is uninterrupted; only a push that would clobber commits
# another chat just pushed fails. A force-push past a peer's commits is
# network-irreversible (no remote reflog) — the worst blast radius in a shared
# tree, so it gets the strongest flow-neutral mechanism: a safer-equivalent.
#
# Emits the PreToolUse updatedInput contract (exit 0 + JSON on stdout) ONLY when
# it rewrites; otherwise stays silent and exits 0 (command runs unchanged).
# Already-`--force-with-lease` and non-force pushes pass through untouched.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $cmd = $data.tool_input.command
    if (-not $cmd) { exit 0 }

    # Only a `git push` invoked at command position is in scope.
    $cp = '(?:^|[;&|]|&&|\|\|)\s*'
    if ($cmd -notmatch "(?i)${cp}git\s+push\b") { exit 0 }
    # Already safe — leave it.
    if ($cmd -match '(?i)--force-with-lease') { exit 0 }

    # Rewrite a bare --force or -f (standalone or bundled, e.g. -fu) on a push.
    # --force / --force=... -> --force-with-lease ; -f token / -f in a flag cluster.
    $new = $cmd
    $new = [regex]::Replace($new, '(?i)--force(?!-with-lease)\b', '--force-with-lease')
    $new = [regex]::Replace($new, '(?<=\s)-([a-eg-zA-EG-Z]*)f([a-eg-zA-EG-Z]*)\b', { param($m) "-$($m.Groups[1].Value)$($m.Groups[2].Value) --force-with-lease" })

    if ($new -eq $cmd) { exit 0 }   # nothing rewritten (no force flag present)

    # Collapse a possible leftover bare `- ` (e.g. `-f` alone became `- --force-...`).
    $new = $new -replace '(?<=\s)-\s+--force-with-lease', '--force-with-lease'
    $new = $new -replace '\s{2,}', ' '

    $out = @{
        hookSpecificOutput = @{
            hookEventName     = 'PreToolUse'
            permissionDecision = 'allow'
            updatedInput      = @{ command = $new }
        }
    }
    # Note to transcript (stderr is informational here; stdout carries the contract).
    [Console]::Error.WriteLine("Force-push auto-safened: --force -> --force-with-lease. A push that would clobber a parallel chat's pushed commits now REFUSES instead of obliterating them (network-irreversible). Override only with an explicit --force-with-lease=<ref> or a deliberate bare --force you re-issue knowing the remote state.")
    [Console]::Out.WriteLine(($out | ConvertTo-Json -Depth 6 -Compress))
    exit 0
} catch {
    exit 0
}
