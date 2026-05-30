# PreToolUse(Write|Edit) — Address Library seed-addition approval reminder.
# WARN-ONLY: never blocks. Flags a Write/Edit that ADDS a row to either curated
# seed CSV (address_names_seed.csv / address_versions_seed.csv). A new seed row
# is a new Address Library DB entity/version, which requires EXPLICIT user
# approval before it lands (data/seeds/policy.md). The warn is a standing
# reminder; the agent confirms approval in-conversation and the review gates
# carry the hard check.
$ErrorActionPreference = 'Stop'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
    $path = $data.tool_input.file_path
    if (-not $path) { exit 0 }

    # Only the two curated seed CSVs are gated. module_seed.csv (module registry)
    # is not an address entity — excluded. Match the leaf regardless of path shape.
    $norm = $path -replace '\\', '/'
    if ($norm -notmatch 'data/seeds/(address_names_seed|address_versions_seed)\.csv$') { exit 0 }
    $leaf = Split-Path -Leaf $path

    # Reconstruct the post-operation content. Write supplies it whole; Edit
    # applies the replacement against the current file. We then count non-comment,
    # non-header data rows added — a pure edit to an existing row (no net new row)
    # does not warn; only a row ADDITION does.
    $oldText = $null
    $newText = $null
    if ($null -ne $data.tool_input.content) {
        # Write: whole-file replace. Old = current on disk (empty if new file).
        if (Test-Path -LiteralPath $path -PathType Leaf) { $oldText = [System.IO.File]::ReadAllText($path) }
        else { $oldText = '' }
        $newText = $data.tool_input.content
    } elseif ($null -ne $data.tool_input.old_string) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { exit 0 }
        $oldText = [System.IO.File]::ReadAllText($path)
        $old = $data.tool_input.old_string
        $new = $data.tool_input.new_string
        if ([bool]$data.tool_input.replace_all) {
            $newText = $oldText.Replace($old, $new)
        } else {
            $idx = $oldText.IndexOf($old)
            if ($idx -lt 0) { exit 0 }
            $newText = $oldText.Substring(0, $idx) + $new + $oldText.Substring($idx + $old.Length)
        }
    } else {
        exit 0
    }

    # A data row is a non-empty line that is not a comment (leading '#') and not
    # the header (the importer reads by DictReader; the header line starts with
    # 'id,' or 'kcdx_id,'). Count data rows on each side; warn only on a net gain.
    function Count-DataRows([string]$text) {
        if ($null -eq $text) { return 0 }
        $n = 0
        foreach ($line in ($text -split "`n")) {
            $t = $line.Trim()
            if ($t -eq '') { continue }
            if ($t.StartsWith('#')) { continue }
            if ($t -match '^(id|kcdx_id),') { continue }
            $n++
        }
        return $n
    }

    $before = Count-DataRows $oldText
    $after  = Count-DataRows $newText
    $added  = $after - $before
    if ($added -le 0) { exit 0 }   # no net new row — a pure in-place edit.

    $msg  = "Address Library seed WARN: this edit adds $added row(s) to $leaf. "
    $msg += "A new seed row is a new Address Library DB entity/version and requires EXPLICIT user approval before it lands (data/seeds/policy.md - 'DB additions require explicit user approval'). "
    $msg += "Confirm the user explicitly approved adding this entity to the DB. An un-approved seed addition is a review-gate finding (AP18)."
    [Console]::Error.WriteLine($msg)
    exit 0
} catch {
    exit 0
}
