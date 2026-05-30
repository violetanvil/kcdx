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
            # Whole-tree sharpening: a reset --hard / clean -f with NO pathspec hits
            # the ENTIRE shared tree (every parallel chat's uncommitted edits), not a
            # scoped subset. Detect "dangerous flag, then nothing but flags/refs to EOL".
            $wholeTree = ''
            if ($cmd -match "(?i)${cp}git\s+reset\b[^\n|&;]*--hard\s*(?:HEAD\S*|@\S*)?\s*(?:[;&|]|$)" -or
                $cmd -match "(?i)${cp}git\s+clean\b[^\n|&;]*-[a-z]*f[a-z]*\s*(?:[;&|]|$)") {
                $wholeTree = " THIS HAS NO PATHSPEC — it hits the ENTIRE shared working tree, including every parallel chat's uncommitted edits. Scope it to the exact paths you confirmed, or you are nuking work you can't see."
            }
            [Console]::Error.WriteLine("Destructive-op BLOCK: $($r.why).$wholeTree Before any irreversible cleanup: (1) RE-CONFIRM the output you're acting on is current — a cancelled or superseded parallel-tool result is NOT a confirmed fact; re-run the read that justified this. (2) `git status` + `git branch --show-current` — the tree is shared with parallel chats (.claude/rules/concurrency-git.md). (3) Stage/commit by exact path; never blanket-delete on a stale reading. If this is genuinely correct after re-confirming, re-issue the command — the block is a deliberate pause, not a veto.")
            exit 2
        }
    }

    # --- SCOPE-FENCE: broad staging across the SHARED index (concurrency-git.md
    # rule 2). The #1 contamination vector — a broad add sweeps ANOTHER chat's
    # in-flight files into your commit. With the session touched-set sidecar
    # (track-touched-files.ps1), this fires ONLY on the contaminating case: a
    # dirty tracked file NOT in this agent's touched set -> BLOCK. All-own-work
    # -> SILENT (the honest broad-add the old always-WARN nagged on). No sidecar
    # yet (no edits / no session_id) -> fall back to the WARN.
    $isBroadStage = ($cmd -match "(?i)${cp}git\s+add\s+(?:-A\b|-u\b|--all\b|\.(?:\s|$))") -or
                    ($cmd -match "(?i)${cp}git\s+commit\b[^\n|&;]*\s-[a-z]*a")
    if ($isBroadStage) {
        $fenceWhy = "broad `git add`/`commit -a` stages across the SHARED index and can sweep another parallel chat's in-flight files into YOUR commit (concurrency-git.md rule 2). Stage by exact path: git add <specific files>"
        $sid = $data.session_id
        $cwd = $data.cwd
        $foreign = $null
        if ($sid -and $cwd) {
            $gitDir = & git -C $cwd rev-parse --git-dir 2>$null
            if ($LASTEXITCODE -eq 0 -and $gitDir) {
                if (-not [System.IO.Path]::IsPathRooted($gitDir)) { $gitDir = Join-Path $cwd $gitDir }
                $sidecar = Join-Path $gitDir ("kcdx-touched-" + ($sid -replace '[^A-Za-z0-9_-]','_') + ".txt")
                if (Test-Path -LiteralPath $sidecar -PathType Leaf) {
                    $touched = @([System.IO.File]::ReadAllLines($sidecar) | Where-Object { $_ })
                    # Dirty TRACKED set: porcelain lines whose index/worktree col is not '?'.
                    $porc = & git -C $cwd status --porcelain 2>$null
                    if ($LASTEXITCODE -eq 0) {
                        $foreign = @()
                        foreach ($line in ($porc -split "`n")) {
                            if ($line.Length -lt 4) { continue }
                            if ($line.Substring(0,2) -eq '??') { continue }   # untracked — a broad add can stage it, but it's not a tracked-file sweep; rule-2 risk is the tracked clobber
                            $p = $line.Substring(3).Trim() -replace '\\','/'
                            # Rename "old -> new": take the destination.
                            if ($p -match '->') { $p = ($p -split '->')[-1].Trim() }
                            if ($touched -notcontains $p) { $foreign += $p }
                        }
                    }
                }
            }
        }
        # $foreign tri-state: $null = couldn't judge (no sidecar/session/cwd) -> WARN;
        # @() = judged, all dirty files are own work -> SILENT OK (the honest broad-add);
        # non-empty = a stranger's file would be swept -> BLOCK.
        if ($null -ne $foreign -and $foreign.Count -gt 0) {
            $shown = ($foreign | Select-Object -First 10) -join ', '
            $more = if ($foreign.Count -gt 10) { " (+$($foreign.Count - 10) more)" } else { '' }
            [Console]::Error.WriteLine("Scope-fence BLOCK: this broad stage would sweep $($foreign.Count) tracked file(s) THIS session never edited — almost certainly another parallel chat's in-flight work (concurrency-git.md rule 2): $shown$more. Stage YOUR files by exact path instead: git add <your specific files>. This is the documented commit-contamination vector; the block is a pause, not a veto — if these files are genuinely yours, add them explicitly.")
            exit 2
        }
        if ($null -ne $foreign) { exit 0 }   # judged clean — silent, no nag on honest own-work staging
        # Couldn't judge (no sidecar yet / no session_id) -> WARN, don't block honest work.
        [Console]::Error.WriteLine("Destructive-op WARN: $fenceWhy. This does not block.")
        exit 0
    }

    # --- WARN set: recoverable / flow-neutral, but surface the parallel-tree risk ---
    # Each proceeds (exit 0); the call is never interrupted. These catch the
    # documented parallel-chat hazards (concurrency-git.md rules 2 + 3) that have
    # no mechanical guard otherwise.
    $warnRules = @(
        @{ rx = "(?i)${cp}git\s+revert\b";                       why = "git revert creates a commit that undoes prior work" }
        @{ rx = "(?i)${cp}git\s+checkout\b[^\n|&;]*\s--\s";      why = "git checkout -- <path> discards working-tree edits to those paths" }
        @{ rx = "(?i)${cp}git\s+restore\b(?![^\n|&;]*--staged)"; why = "git restore <path> discards working-tree edits to those paths" }
        # Shared-tree mutation — concurrency-git.md rule 3. checkout <branch> /
        # switch / stash all change the ONE working tree every chat shares.
        @{ rx = "(?i)${cp}git\s+checkout\s+(?!-{1,2}(?:\s|$))(?!-b\b)\S";  why = "`git checkout <branch>` switches the SHARED working tree — it changes the branch (and can clobber uncommitted edits) for every parallel chat, not just this one (concurrency-git.md rule 3). Don't switch branches on your own initiative" }
        @{ rx = "(?i)${cp}git\s+switch\b(?![^\n|&;]*-c\b)";         why = "`git switch` changes the SHARED working tree's branch for every parallel chat (concurrency-git.md rule 3). Don't switch on your own initiative" }
        @{ rx = "(?i)${cp}git\s+stash\b";                          why = "`git stash` mutates the SHARED working tree — `stash push -u` SILENTLY captures untracked files another chat authored (concurrency-git.md rule 3). Don't stash on your own initiative" }
    )
    foreach ($r in $warnRules) {
        if ($cmd -match $r.rx) {
            [Console]::Error.WriteLine("Destructive-op WARN: $($r.why). Confirm you're acting on CURRENT state, not a stale/cancelled parallel-tool result, and remember the tree is shared with parallel chats (.claude/rules/concurrency-git.md §Destructive ops). This does not block.")
            exit 0
        }
    }
    exit 0
} catch {
    exit 0
}
