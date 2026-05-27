# PreToolUse(Edit|Write) — anti-pattern consent gate.
# Forces a user accept-prompt on any write to the anti-pattern catalog or its
# rationale record (permissionDecision: "ask"). Does NOT block: the user clicks
# allow. An agent cannot silently add/change an anti-pattern or its why.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path

    if (-not $path) { exit 0 }

    # Match either governance file regardless of absolute/relative path shape.
    $leaf = Split-Path -Leaf $path
    if ($leaf -ne 'anti-patterns.md' -and $leaf -ne 'anti-pattern-rationale.md') { exit 0 }

    # Only gate the kcdx governance copies (rules/ + .claude/), not a same-named
    # file elsewhere.
    $norm = $path -replace '\\', '/'
    if ($norm -notmatch '\.claude/(rules/)?anti-patterns?(-rationale)?\.md$') { exit 0 }

    $decision = @{
        hookSpecificOutput = @{
            hookEventName            = 'PreToolUse'
            permissionDecision       = 'ask'
            permissionDecisionReason = "Editing the anti-pattern catalog/rationale ($leaf) requires user consent — an anti-pattern is a blessed rule. Confirm you approve this change."
        }
    } | ConvertTo-Json -Depth 5 -Compress

    [Console]::Out.WriteLine($decision)
    exit 0
} catch {
    exit 0
}
