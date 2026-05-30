# PostToolUse(Edit|Write) — session touched-set recorder
# (.claude/rules/concurrency-git.md §"Destructive ops" — scope-fence support).
# Appends the repo-relative path of every file THIS agent session edits to a
# per-session sidecar `.git/kcdx-touched-<session_id>.txt`. guard-destructive-ops.ps1's
# scope-fence reads it to tell "my own work" from "a parallel chat's file" on a
# broad `git add`. Never blocks, never errors — pure recording.
#
# Keyed by session_id (each parallel chat has a distinct one), so the sets don't
# cross-contaminate. Stored under .git/ (not the worktree) so it never shows in
# `git status` and never gets staged. .git/ is per-worktree, so a worktree-isolated
# agent gets its own sidecar automatically.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    $sid = $data.session_id
    $cwd = $data.cwd
    if (-not $path -or -not $sid) { exit 0 }

    # Resolve the git dir from cwd; silent outside a repo.
    $gitDir = & git -C $cwd rev-parse --git-dir 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $gitDir) { exit 0 }
    if (-not [System.IO.Path]::IsPathRooted($gitDir)) {
        $gitDir = Join-Path $cwd $gitDir
    }

    # Repo-relative, forward-slashed (matches `git status --porcelain` paths).
    $root = & git -C $cwd rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $root) { exit 0 }
    $full = [System.IO.Path]::GetFullPath($path)
    $rootFull = [System.IO.Path]::GetFullPath($root)
    if (-not $full.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { exit 0 }
    $rel = $full.Substring($rootFull.Length).TrimStart('\','/') -replace '\\','/'
    if (-not $rel) { exit 0 }

    $sidecar = Join-Path $gitDir ("kcdx-touched-" + ($sid -replace '[^A-Za-z0-9_-]','_') + ".txt")
    # Append only if not already present (keep the file small; set semantics).
    $existing = @()
    if (Test-Path -LiteralPath $sidecar -PathType Leaf) {
        $existing = [System.IO.File]::ReadAllLines($sidecar)
    }
    if ($existing -notcontains $rel) {
        Add-Content -LiteralPath $sidecar -Value $rel -Encoding UTF8
    }
    exit 0
} catch {
    exit 0
}
