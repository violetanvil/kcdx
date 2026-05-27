# Linear scan of WHGame.dll .text for `lea reg, [rip+disp32]` instructions
# whose target resolves to one of the combat-restriction strings.
#
# x86-64 LEA encoding: REX (0x48..0x4F) + 0x8D + ModR/M + disp32
# We accept any REX byte 0x48-0x4F and any ModR/M whose mod==00 and rm==101 (RIP+disp32).
# That's ModR/M bytes: 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D (any reg field).

$dll = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll'
$bytes = [System.IO.File]::ReadAllBytes($dll)

# PE parse
$peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
$coff = $peOff + 4
$numSec = [System.BitConverter]::ToUInt16($bytes, $coff + 2)
$optSize = [System.BitConverter]::ToUInt16($bytes, $coff + 16)
$imageBase = [System.BitConverter]::ToUInt64($bytes, $coff + 20 + 24)
$secStart = $coff + 20 + $optSize

# Locate .text and .rdata
$textVa = 0; $textFileOff = 0; $textSize = 0
$rdataVa = 0; $rdataFileOff = 0; $rdataSize = 0
for ($i = 0; $i -lt $numSec; $i++) {
    $s = $secStart + $i * 40
    $name = [System.Text.Encoding]::ASCII.GetString($bytes, $s, 8).TrimEnd([char]0)
    $vAddr = [System.BitConverter]::ToUInt32($bytes, $s + 12)
    $vSize = [System.BitConverter]::ToUInt32($bytes, $s + 8)
    $rOff = [System.BitConverter]::ToUInt32($bytes, $s + 20)
    if ($name -eq '.text')  { $textVa = $vAddr;  $textFileOff = $rOff;  $textSize = $vSize }
    if ($name -eq '.rdata') { $rdataVa = $vAddr; $rdataFileOff = $rOff; $rdataSize = $vSize }
}

Write-Output "ImageBase=0x$('{0:X}' -f $imageBase)  .text VA=0x$('{0:X}' -f $textVa) size=0x$('{0:X}' -f $textSize) fileOff=0x$('{0:X}' -f $textFileOff)"

# Target string VAs (file offsets we already found, translated to RVAs and VAs)
$targets = @(
    @{name='cant_change_outfit_in_combat'; fileOff = 0x3EDDC60},
    @{name='cant_use_in_combat';           fileOff = 0x3EDD000}
)
foreach ($t in $targets) {
    $rva = ($t.fileOff - $rdataFileOff) + $rdataVa
    $t.va = $imageBase + $rva
    Write-Output ("Target: {0}  file=0x{1:X} VA=0x{2:X}" -f $t.name, $t.fileOff, $t.va)
}

# ModR/M bytes meaning RIP-relative (mod=00, rm=101, any reg field): 0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D
$ripModRM = @(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D)
$ripModRMSet = @{}
foreach ($b in $ripModRM) { $ripModRMSet[$b] = $true }

# Scan .text
$textStart = $textFileOff
$textEnd = $textFileOff + $textSize - 7  # 7-byte instruction
Write-Output "Scanning .text from file 0x$('{0:X}' -f $textStart) to 0x$('{0:X}' -f $textEnd) ..."

$results = @()
for ($i = $textStart; $i -lt $textEnd; $i++) {
    $b0 = $bytes[$i]
    # REX byte: 0x48-0x4F (any combination of REX.W/R/X/B with W set; we accept all REX with W=1)
    if ($b0 -lt 0x48 -or $b0 -gt 0x4F) { continue }
    if ($bytes[$i+1] -ne 0x8D) { continue }
    $modrm = $bytes[$i+2]
    if (-not $ripModRMSet[[int]$modrm]) { continue }
    # disp32 is at i+3..i+6, RIP base for the displacement is i+7 (next instruction's file-relative)
    $disp = [System.BitConverter]::ToInt32($bytes, $i + 3)
    $insnEndFileOff = $i + 7
    $insnEndRva = ($insnEndFileOff - $textFileOff) + $textVa
    $insnEndVa = $imageBase + $insnEndRva
    $targetVa = [int64]$insnEndVa + [int64]$disp
    foreach ($t in $targets) {
        if ($targetVa -eq $t.va) {
            $insnStartFileOff = $i
            $insnStartRva = ($i - $textFileOff) + $textVa
            $insnStartVa = $imageBase + $insnStartRva
            $regField = (($modrm -shr 3) -band 0x07)
            # If REX.R is set (bit 2 of REX low nibble: 0x44), add 8 to reg
            if (($b0 -band 0x04) -ne 0) { $regField += 8 }
            $regs = @('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15')
            $results += [pscustomobject]@{
                Target   = $t.name
                InsnVA   = ('0x{0:X}' -f $insnStartVa)
                FileOff  = ('0x{0:X}' -f $insnStartFileOff)
                Bytes    = ((0..6) | ForEach-Object { '{0:X2}' -f $bytes[$i+$_] }) -join ' '
                Reg      = $regs[$regField]
            }
        }
    }
}

Write-Output ""
Write-Output "=== Matches ==="
$results | Format-Table -AutoSize
Write-Output ""
Write-Output "Total: $($results.Count)"

# Save results for downstream byte dumps
$results | ConvertTo-Json -Depth 4 | Set-Content "$PSScriptRoot\xrefs.json"
Write-Output "Saved to xrefs.json"
