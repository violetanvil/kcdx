# PostToolUse(Bash|PowerShell) — dynamic-deletion tripwire
# (.claude/rules/concurrency-git.md §"Destructive ops" — dynamic backstop).
# DETECTION-ONLY: the action already ran; this never blocks. Closes the gap the
# PreToolUse command-text guards are blind to — a deletion done by an invoked
# SCRIPT or a variable/glob-expanded rm, where the dangerous part isn't a literal
# token the PreToolUse rules can match.
#
# ARMED (option 3) only when the command could have deleted dynamically — a
# script/interpreter invocation, or an rm/Remove-Item/del with a variable or glob.
# A plain `ls` / `git status` / `git add <path>` early-exits BEFORE the ~26ms
# `git status`, so the marginal cost lands only on plausibly-deleting calls.
# (The ~144ms pwsh spinup is paid by any registered hook regardless — unavoidable.)
#
# FIRES (loud WARN) when the post-command working tree shows an UNSTAGED tracked
# deletion ( D in porcelain): a file vanished from disk that the agent did NOT
# stage as a removal. A staged deletion (D , from `git rm` / `git add` of a
# removal) is intended and stays quiet.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $cmd = $data.tool_input.command
    if (-not $cmd) { exit 0 }

    # --- ARM check: is this command capable of a dynamic deletion? ---
    # Script / interpreter invocation: bash/pwsh/sh/python..., a *.ps1/*.sh, `& <exe>`,
    # or a `.\script` / `./script` launch.
    $scriptInvoke = $cmd -match '(?i)(?:^|[;&|]|&&|\|\|)\s*(?:bash|pwsh|powershell|sh|python|node|&|\.[\\/]|\./)\b' -or
                    $cmd -match '(?i)\.(?:ps1|sh|bat|cmd|py)\b'
    # rm/Remove-Item/del whose target involves a variable ($x / %x% / $env:) or a
    # glob (*?[) — i.e. not a fully-literal path the PreToolUse guard already parsed.
    $dynamicRm = $cmd -match '(?i)(?:^|[;&|]|&&|\|\|)\s*(?:rm|del|Remove-Item)\b[^\n]*(?:\$\w|\$\{|\$env:|%\w+%|[*?\[])'

    if (-not ($scriptInvoke -or $dynamicRm)) { exit 0 }

    # --- Consequence check: any UNSTAGED tracked deletion in the worktree? ---
    # Resolve repo root from cwd; stay silent (never error) outside a repo.
    $root = & git rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $root) { exit 0 }
    $porcelain = & git -C $root status --porcelain 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $porcelain) { exit 0 }

    # Porcelain XY PATH. Y (col 2) = worktree state. ' D' = deleted-unstaged.
    # Exclude 'D ' (staged deletion = intended) and 'AD'/'RD' index-side records.
    $deleted = @()
    foreach ($line in ($porcelain -split "`n")) {
        if ($line.Length -lt 4) { continue }
        $y = $line.Substring(1, 1)            # worktree column
        $x = $line.Substring(0, 1)            # index column
        if ($y -eq 'D' -and $x -ne 'D') {     # unstaged delete the agent didn't stage
            $deleted += $line.Substring(3)
        }
    }
    if ($deleted.Count -eq 0) { exit 0 }

    $shown = ($deleted | Select-Object -First 10) -join ', '
    $more = if ($deleted.Count -gt 10) { " (+$($deleted.Count - 10) more)" } else { '' }
    [Console]::Error.WriteLine("Dynamic-deletion TRIPWIRE: the last command left $($deleted.Count) tracked file(s) deleted-but-UNSTAGED in the working tree — a deletion NOT staged as a `git rm`, so it likely came from an invoked script or a variable/glob-expanded rm the command-text guard couldn't see: $shown$more. CONFIRM this was intended. If NOT: the files are still recoverable — `git checkout -- <path>` (or `git restore <path>`) restores them from HEAD; do this BEFORE any commit. The tree is shared with parallel chats (.claude/rules/concurrency-git.md), so a deletion you didn't intend may be clobbering another chat's work. Detection-only: this did not block.")
    exit 0
} catch {
    exit 0
}
