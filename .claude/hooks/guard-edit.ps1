# PreToolUse(Edit) — C++/Lua source file growth guard.
# WARN-ONLY phase: never blocks. Files already at/above 300 are grandfathered.
# Warns when an edit grows a sub-300 file toward or across the ~300 split line.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    $old = $data.tool_input.old_string
    $new = $data.tool_input.new_string
    $replaceAll = [bool]$data.tool_input.replace_all

    if (-not $path) { exit 0 }
    if ($path -notmatch '\.(cpp|h|hpp|cc|cxx|inl|lua)$') { exit 0 }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }

    $content = [System.IO.File]::ReadAllText($path)
    $currentLines = ($content -split "`n").Length

    # Already over the line — grandfathered; don't nag every edit to a big file.
    if ($currentLines -ge 300) { exit 0 }

    if ($replaceAll) {
        $newContent = $content.Replace($old, $new)
    } else {
        $idx = $content.IndexOf($old)
        if ($idx -lt 0) { exit 0 }
        $newContent = $content.Substring(0, $idx) + $new + $content.Substring($idx + $old.Length)
    }

    $newLines = ($newContent -split "`n").Length

    if ($newLines -ge 300) {
        [Console]::Error.WriteLine("Edit WARN: $path would grow from $currentLines to $newLines lines (CLAUDE.md: review for split past ~300). Split one-file-one-concern before adding more.")
        exit 0
    }
    if ($newLines -ge 200) {
        [Console]::Error.WriteLine("Edit WARN: $path would grow from $currentLines to $newLines lines (warn >=200; ~300 is the split-review line). Plan the next split now.")
        exit 0
    }
    exit 0
} catch {
    exit 0
}
