# PreToolUse(Write|Edit) — code-review snapshot immutability guard.
# BLOCKS (exit 2) edits to per-commit review snapshots. Not a code-style
# heuristic — these snapshots are immutable by design; a fixed finding is
# tracked by the NEXT /code-review producing a fresh snapshot at the new HEAD
# hash, never by editing a prior one. /code-review writes them via Bash heredoc,
# which does not invoke the Write/Edit tool and so bypasses this hook.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }

    # Match .claude/skills/code-review/<branch>/<hash>/*.md — two subdirs after
    # code-review/. SKILL.md (direct child) stays editable.
    if ($path -match '\.claude[\\/]skills[\\/]code-review[\\/][^\\/]+[\\/][^\\/]+[\\/].*\.md$') {
        [Console]::Error.WriteLine("Write/Edit blocked: $path is a code-review snapshot. Per-commit review snapshots are immutable to working agents — only /code-review writes them (via Bash heredoc, bypassing this hook). A finding that's been addressed is tracked by the next /code-review run producing a fresh snapshot at the new HEAD hash, NOT by editing the prior snapshot.")
        exit 2
    }
    exit 0
} catch {
    exit 0
}
