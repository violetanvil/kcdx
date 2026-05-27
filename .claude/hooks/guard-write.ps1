# PreToolUse(Write) — new C++/Lua source file size guard.
# WARN-ONLY phase: never blocks. New files only; existing files grandfathered
# (a Write that overwrites an existing file passes through). Promote the >=300
# branch to `exit 2` once the false-positive rate is understood.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    $content = $data.tool_input.content
    if (-not $content) { exit 0 }
    if ($path -notmatch '\.(cpp|h|hpp|cc|cxx|inl|lua)$') { exit 0 }

    # Existing files are grandfathered — Write overwrites pass through.
    if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }

    $lines = ($content -split "`n").Length

    if ($lines -gt 300) {
        [Console]::Error.WriteLine("Write WARN: new file $path is $lines lines (CLAUDE.md: review for split past ~300). Consider splitting one-file-one-concern before this grows further.")
        exit 0
    }
    if ($lines -ge 200) {
        [Console]::Error.WriteLine("Write WARN: new file $path is $lines lines (warn >=200; ~300 is the split-review line). Plan the split now.")
        exit 0
    }
    exit 0
} catch {
    exit 0
}
