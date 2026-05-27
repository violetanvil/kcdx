$dll = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGameArm.dll'
$bytes = [System.IO.File]::ReadAllBytes($dll)
$peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
$coff = $peOff + 4
$numSec = [System.BitConverter]::ToUInt16($bytes, $coff + 2)
$optSize = [System.BitConverter]::ToUInt16($bytes, $coff + 16)
$imageBase = [System.BitConverter]::ToUInt64($bytes, $coff + 20 + 24)
$secStart = $coff + 20 + $optSize
$textVa = 0; $textFileOff = 0; $textSize = 0
for ($i = 0; $i -lt $numSec; $i++) {
    $s = $secStart + $i*40
    $name = [System.Text.Encoding]::ASCII.GetString($bytes, $s, 8).TrimEnd([char]0)
    if ($name -eq '.text') { $textVa = [System.BitConverter]::ToUInt32($bytes, $s+12); $textFileOff = [System.BitConverter]::ToUInt32($bytes, $s+20); $textSize = [System.BitConverter]::ToUInt32($bytes, $s+8) }
}

$targets = @(
    @{name='cant_change_outfit_in_combat'; va = 0x184EFB5A0},
    @{name='cant_use_in_combat';           va = 0x184EFA510}
)

$ripModRMSet = @{}; foreach ($b in @(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D)) { $ripModRMSet[$b] = $true }

$textStart = $textFileOff
$textEnd = $textFileOff + $textSize - 7

Write-Output "Scanning .text (0x$('{0:X}' -f $textSize) bytes) ..."
$sw = [System.Diagnostics.Stopwatch]::StartNew()

$results = New-Object System.Collections.Generic.List[object]
for ($i = $textStart; $i -lt $textEnd; $i++) {
    $b0 = $bytes[$i]
    if ($b0 -lt 0x48 -or $b0 -gt 0x4F) { continue }
    if ($bytes[$i+1] -ne 0x8D) { continue }
    $modrm = $bytes[$i+2]
    if (-not $ripModRMSet[[int]$modrm]) { continue }
    $disp = [System.BitConverter]::ToInt32($bytes, $i + 3)
    $insnEndVa = $imageBase + (($i + 7 - $textFileOff) + $textVa)
    $targetVa = [int64]$insnEndVa + [int64]$disp
    foreach ($t in $targets) {
        if ($targetVa -eq $t.va) {
            $insnStartVa = $imageBase + (($i - $textFileOff) + $textVa)
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
Write-Output "Scan took $($sw.Elapsed.TotalSeconds) seconds"
Write-Output ""
Write-Output "=== Matches ==="
$results | Format-Table -AutoSize
Write-Output ""
Write-Output "Total: $($results.Count)"
$results | ConvertTo-Json -Depth 4 | Set-Content "$PSScriptRoot\xrefs_arm.json"
Write-Output "Saved to xrefs_arm.json"
