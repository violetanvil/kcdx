# PreToolUse(Bash|PowerShell) — destructive-op confirmation guard
# (.claude/rules/concurrency-git.md §"Destructive ops").
# TIERED: BLOCK (exit 2) the irreversible-without-reflog set; WARN (exit 0) the
# recoverable set. A destructive shell command acting on stale/cancelled
# parallel-tool output is the misfire this guards — the agent must re-confirm
# live state (re-read the output it's acting on, `git status`) before re-issuing.
#
# BLOCK set (irreversible without git reflog / unrecoverable from disk):
#   rm -rf / -r / Remove-Item -Recurse, git clean -f/-fd/-fdx, git reset --hard.
# WARN set (recoverable: revert is a new commit, checkout -- touches tracked files
#   restorable from HEAD): git revert, git checkout -- <path>, git restore <path>.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $cmd = $data.tool_input.command
    if (-not $cmd) { exit 0 }

    # COMMAND-POSITION anchor: the destructive verb must be the command being
    # INVOKED — at string start or right after a shell operator (; && || | &) —
    # not text sitting inside a quoted arg / here-string. This is what keeps a
    # `git commit -m '...rm -r...'` message body from tripping the rm rule.
    # Bare newline is deliberately NOT a separator: here-string/message bodies
    # contain newlines, and an `rm` on a message line is prose, not a command.
    $cp = '(?:^|[;&|]|&&|\|\|)\s*'

    # --- BLOCK set: irreversible without reflog / unrecoverable from disk ---
    $blockRules = @(
        @{ rx = "(?i)${cp}rm\b[^\n|&;]*\s-[a-z]*r";              why = "rm -r/-rf permanently deletes files (no reflog, no recycle bin)" }
        @{ rx = "(?i)${cp}Remove-Item\b[^\n|&;]*-Recurse";       why = "Remove-Item -Recurse permanently deletes a tree" }
        @{ rx = "(?i)${cp}git\s+clean\b[^\n|&;]*-[a-z]*f";       why = "git clean -f deletes untracked files irrecoverably (not in any commit or reflog)" }
        @{ rx = "(?i)${cp}git\s+reset\b[^\n|&;]*--hard";         why = "git reset --hard discards uncommitted changes in the SHARED working tree (clobbers parallel chats' edits too)" }
    )
    foreach ($r in $blockRules) {
        if ($cmd -match $r.rx) {
            [Console]::Error.WriteLine("Destructive-op BLOCK: $($r.why). Before any irreversible cleanup: (1) RE-CONFIRM the output you're acting on is current — a cancelled or superseded parallel-tool result is NOT a confirmed fact; re-run the read that justified this. (2) `git status` + `git branch --show-current` — the tree is shared with parallel chats (.claude/rules/concurrency-git.md). (3) Stage/commit by exact path; never blanket-delete on a stale reading. If this is genuinely correct after re-confirming, re-issue the command — the block is a deliberate pause, not a veto.")
            exit 2
        }
    }

    # --- WARN set: recoverable, but still confirm live state first ---
    $warnRules = @(
        @{ rx = "(?i)${cp}git\s+revert\b";                       why = "git revert creates a commit that undoes prior work" }
        @{ rx = "(?i)${cp}git\s+checkout\b[^\n|&;]*\s--\s";      why = "git checkout -- <path> discards working-tree edits to those paths" }
        @{ rx = "(?i)${cp}git\s+restore\b(?![^\n|&;]*--staged)"; why = "git restore <path> discards working-tree edits to those paths" }
    )
    foreach ($r in $warnRules) {
        if ($cmd -match $r.rx) {
            [Console]::Error.WriteLine("Destructive-op WARN: $($r.why). Confirm you're acting on CURRENT state, not a stale/cancelled parallel-tool result — re-read the output that justified this and check `git status` before proceeding (.claude/rules/concurrency-git.md §Destructive ops). Recoverable, so this does not block.")
            exit 0
        }
    }
    exit 0
} catch {
    exit 0
}
