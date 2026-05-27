# PreToolUse(Write|Edit) — C++ comment-density guard.
# WARN-ONLY: never blocks. Flags files that are mostly comments (history,
# restated purpose, quoted MSDN/wiki paragraphs) over actual code.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }
    if ($path -notmatch '\.(cpp|h|hpp|cc|cxx|inl)$') { exit 0 }

    # Post-operation content (Write supplies it; Edit applies the replacement).
    $content = $null
    if ($data.tool_input.content) {
        $content = $data.tool_input.content
    } elseif ($data.tool_input.old_string) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }
        $current = [System.IO.File]::ReadAllText($path)
        $old = $data.tool_input.old_string
        $new = $data.tool_input.new_string
        if ([bool]$data.tool_input.replace_all) {
            $content = $current.Replace($old, $new)
        } else {
            $idx = $current.IndexOf($old)
            if ($idx -lt 0) { exit 0 }
            $content = $current.Substring(0, $idx) + $new + $current.Substring($idx + $old.Length)
        }
    }
    if (-not $content) { exit 0 }

    $lines = $content -split "`n"
    $total_nonblank = 0
    $comment_lines = 0
    $in_block = $false

    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "") { continue }
        $total_nonblank++

        if ($in_block) {
            $comment_lines++
            if ($trimmed -match '\*/') { $in_block = $false }
            continue
        }
        if ($trimmed -match '^/\*') {
            $comment_lines++
            if ($trimmed -notmatch '\*/') { $in_block = $true }
        } elseif ($trimmed -match '^//') {
            $comment_lines++
        }
    }

    # Ratio only meaningful on files with enough lines — small stubs run high.
    if ($total_nonblank -ge 30) {
        $ratio = [math]::Round(($comment_lines / $total_nonblank) * 100)
        if ($ratio -gt 60) {
            [Console]::Error.WriteLine("Comment-density WARN: $path is $ratio% comments by line ($comment_lines/$total_nonblank). Target <60%. Drop history, restated purpose, and quoted MSDN/wiki paragraphs — keep intent + WHY + // SOURCE: only.")
        }
    }
    exit 0
} catch {
    exit 0
}
