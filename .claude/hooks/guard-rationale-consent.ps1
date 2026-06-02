# PreToolUse(Edit|Write) — anti-pattern RATIONALE consent gate (repo-specific).
# Forces a user accept-prompt (permissionDecision: "ask") on any write to the
# anti-pattern rationale record. The system guard-anti-pattern-consent.py gates
# .claude/rules/anti-patterns.md; this re-applies the same gate to the rationale
# file, which no system hook covers. Does NOT block: the user clicks allow.
# An agent cannot silently add/change an AP's blessed rationale.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path

    if (-not $path) { exit 0 }

    # Gate the rationale file ONLY — never anti-patterns.md (the system guard owns
    # that), never a same-named file elsewhere.
    $leaf = Split-Path -Leaf $path
    if ($leaf -ne 'anti-pattern-rationale.md') { exit 0 }

    # Only the kcdx governance copy under .claude/, not a same-named file elsewhere.
    $norm = $path -replace '\\', '/'
    if ($norm -notmatch '\.claude/anti-pattern-rationale\.md$') { exit 0 }

    $decision = @{
        hookSpecificOutput = @{
            hookEventName            = 'PreToolUse'
            permissionDecision       = 'ask'
            permissionDecisionReason = "Editing the anti-pattern rationale record requires user consent — an AP's rationale is a blessed record; confirm you approve this change."
        }
    } | ConvertTo-Json -Depth 5 -Compress

    [Console]::Out.WriteLine($decision)
    exit 0
} catch {
    exit 0
}
