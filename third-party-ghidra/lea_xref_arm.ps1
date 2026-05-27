# LEA xref scan on WHGameArm.dll for the combat strings.
$dll = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGameArm.dll'
$bytes = [System.IO.File]::ReadAllBytes($dll)
$peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
$coff = $peOff + 4
$numSec = [System.BitConverter]::ToUInt16($bytes, $coff + 2)
$optSize = [System.BitConverter]::ToUInt16($bytes, $coff + 16)
$imageBase = [System.BitConverter]::ToUInt64($bytes, $coff + 20 + 24)
$secStart = $coff + 20 + $optSize
$textVa = 0; $textFileOff = 0; $textSize = 0
$rdataVa = 0; $rdataFileOff = 0; $rdataSize = 0
for ($i = 0; $i -lt $numSec; $i++) {
    $s = $secStart + $i * 40
    $name = [System.Text.Encoding]::ASCII.GetString($bytes, $s, 8).TrimEnd([char]0)
    if ($name -eq '.text') { $textVa = [System.BitConverter]::ToUInt32($bytes, $s+12); $textFileOff = [System.BitConverter]::ToUInt32($bytes, $s+20); $textSize = [System.BitConverter]::ToUInt32($bytes, $s+8) }
    if ($name -eq '.rdata') { $rdataVa = [System.BitConverter]::ToUInt32($bytes, $s+12); $rdataFileOff = [System.BitConverter]::ToUInt32($bytes, $s+20); $rdataSize = [System.BitConverter]::ToUInt32($bytes, $s+8) }
}

$targets = @(
    @{name='cant_change_outfit_in_combat'; fileOff = 0x4EF9DA0},
    @{name='cant_use_in_combat';           fileOff = 0x4EF8D10}
)
foreach ($t in $targets) {
    $t.va = $imageBase + (($t.fileOff - $rdataFileOff) + $rdataVa)
    Write-Output ("{0}: VA=0x{1:X}" -f $t.name, $t.va)
}

$ripModRMSet = @{}; foreach ($b in @(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D)) { $ripModRMSet[$b] = $true }
$end = $textFileOff + $textSize - 7
$results = New-Object System.Collections.Generic.List[object]

Write-Output ("Scanning .text (size 0x{0:X}) ..." -f $textSize)
$sw = [System.Diagnostics.Stopwatch]::StartNew()

for ($i = $textFileOff; $i -lt $end; $i++) {
    $b0 = $bytes[$i]
    if ($b0 -lt 0x48 -or $b0 -gt 0x4F) { continue }
    if ($bytes[$i+1] -ne 0x8D) { continue }
    if (-not $ripModRMSet[[int]$bytes[$i+2]]) { continue }
    $disp = [System.BitConverter]::ToInt32($bytes, $i+3)
    $insnEndVa = [int64]($imageBase + ($i + 7 - $textFileOff) + $textVa)
    $resolved = $insnEndVa + $disp
    foreach ($t in $targets) {
        if ($resolved -eq $t.va) {
            $insnStartVa = [int64]($imageBase + ($i - $textFileOff) + $textVa)
            $modrm = $bytes[$i+2]
            $regField = (($modrm -shr 3) -band 0x07)
            if (($b0 -band 0x04) -ne 0) { $regField += 8 }
            $regs = @('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15')
            $results.Add([pscustomobject]@{
                Target  = $t.name
                InsnVA  = ('0x{0:X}' -f $insnStartVa)
                FileOff = ('0x{0:X}' -f $i)
                Bytes   = ((0..6) | ForEach-Object { '{0:X2}' -f $bytes[$i+$_] }) -join ' '
                Reg     = $regs[$regField]
            }) | Out-Null
        }
    }
}

$sw.Stop()
Write-Output ("Scan took {0:F1}s" -f $sw.Elapsed.TotalSeconds)
Write-Output ""
$results | Format-Table -AutoSize
Write-Output ("Total: {0}" -f $results.Count)
$results | ConvertTo-Json -Depth 4 | Set-Content "$PSScriptRoot\xrefs_arm_lea.json"
