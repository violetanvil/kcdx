# PreToolUse(Bash|PowerShell) — shared-index lock-collision guard
# (.claude/rules/concurrency-git.md §"Destructive ops" — parallel-index race).
# Parallel chats share ONE .git. When chat A holds .git/index.lock mid-mutation,
# chat B's index-mutating command races it — corruption or a hard error. This
# turns that race into a clean BLOCK-and-retry.
#
# Two branches, both narrowly armed (near-zero false positive):
#   1) Index-mutating git command while a LIVE lock is held -> BLOCK (retry).
#      Stale lock (old, no live git process) -> WARN, pass (don't block on debris).
#   2) A command DELETING a .git/*.lock that is LIVE -> BLOCK (reaping an active
#      lock = two writers in the index, the worst outcome). Stale -> allow.
# Plain reads (status/log/diff/show) and non-git commands early-exit.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $cmd = $data.tool_input.command
    $cwd = $data.cwd
    if (-not $cmd) { exit 0 }

    $cp = '(?:^|[;&|]|&&|\|\|)\s*'

    # Arm only when relevant: an index-mutating git verb, OR a delete targeting a
    # .git lock. Everything else (the overwhelming majority) exits before any IO.
    $mutating = $cmd -match "(?i)${cp}git\s+(?:add|commit|rm|mv|stash|merge|rebase|cherry-pick|am|reset|restore|checkout|pull|apply|update-index)\b"
    $lockDelete = $cmd -match "(?i)${cp}(?:rm|del|Remove-Item)\b[^\n]*\.git[\\/](?:[\w-]+[\\/])*(?:index|config|HEAD|[\w-]+)\.lock"
    if (-not ($mutating -or $lockDelete)) { exit 0 }

    $gitDir = & git -C $cwd rev-parse --git-dir 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $gitDir) { exit 0 }
    if (-not [System.IO.Path]::IsPathRooted($gitDir)) { $gitDir = Join-Path $cwd $gitDir }
    $lock = Join-Path $gitDir 'index.lock'
    if (-not (Test-Path -LiteralPath $lock -PathType Leaf)) { exit 0 }   # no lock -> nothing to race

    # Lock exists. Live or stale? Live = young OR a git.exe is currently running.
    $ageSec = ([System.DateTime]::UtcNow - [System.IO.File]::GetLastWriteTimeUtc($lock)).TotalSeconds
    $gitRunning = $false
    try { $gitRunning = [bool](Get-Process -Name git -ErrorAction Stop) } catch { $gitRunning = $false }
    $live = ($ageSec -lt 30) -or $gitRunning

    if ($lockDelete) {
        if ($live) {
            [Console]::Error.WriteLine("Git-lock BLOCK: this command deletes a LIVE .git lock (held <30s ago, or a git process is running). Reaping an active lock lets TWO writers into the shared index at once — the worst corruption in a parallel-chat tree (.claude/rules/concurrency-git.md). Another chat is mid-commit; WAIT and retry your own command. Only reap a lock confirmed STALE (old + no running git).")
            exit 2
        }
        exit 0   # stale lock — letting the agent clear debris is fine
    }

    # Index-mutating command with a lock present.
    if ($live) {
        [Console]::Error.WriteLine("Git-lock BLOCK: .git/index.lock is held by a LIVE git operation — a parallel chat is mid-mutation of the SHARED index (.claude/rules/concurrency-git.md). Running your git command now races it (corruption or hard error). RE-RUN in a moment; the lock clears when the other chat's commit finishes. This is a retry, not a veto. Do NOT delete the lock — that command is also blocked.")
        exit 2
    }
    # Stale lock — surface it but don't block (the agent may legitimately need to clear it).
    [Console]::Error.WriteLine("Git-lock WARN: .git/index.lock exists but looks STALE ($([int]$ageSec)s old, no running git) — likely debris from a crashed/cancelled git op, not an active parallel chat. Your command will fail until it's cleared. If you confirm no git is running, removing the single lock file is safe; do NOT reach for `git reset`/`clean` (.claude/rules/concurrency-git.md).")
    exit 0
} catch {
    exit 0
}
