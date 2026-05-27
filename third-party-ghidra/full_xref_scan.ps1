# Exhaustive xref hunt: scan WHGame.dll and WHGameArm.dll for every byte sequence
# that could be a reference to our two target strings.
#
# We look for:
#   - 8-byte LE: the full VA (matches MOV reg64, imm64; also any 64-bit pointer in data)
#   - 4-byte LE: the RVA (matches RIP-relative LEA disp32 when the next-insn-addr happens to
#       leave disp == rva; rare exact match, but worth checking)
#   - 4-byte LE: each possible disp32 value, i.e. computed per insn position. This is what
#       my earlier LEA scan did but limited to LEA opcodes. Here we cover any displacement.
#
# We also scan EVERY section, not just .text. That catches vtables and data tables.

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.IO;

public class Scanner {
    public static List<long> FindBytes(byte[] data, byte[] needle, int maxResults) {
        var results = new List<long>();
        int n = needle.Length;
        long end = data.Length - n;
        for (long i = 0; i < end; i++) {
            bool match = true;
            for (int j = 0; j < n; j++) {
                if (data[i + j] != needle[j]) { match = false; break; }
            }
            if (match) {
                results.Add(i);
                if (results.Count >= maxResults) break;
            }
        }
        return results;
    }
}
"@

function Scan-Dll {
    param(
        [string]$path,
        [hashtable]$targets    # name -> hashtable with: fileOff, expectedVa (computed below)
    )
    Write-Output ""
    Write-Output ("=== {0} ===" -f (Split-Path -Leaf $path))
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
    $coff = $peOff + 4
    $numSec = [System.BitConverter]::ToUInt16($bytes, $coff + 2)
    $optSize = [System.BitConverter]::ToUInt16($bytes, $coff + 16)
    $imageBase = [System.BitConverter]::ToUInt64($bytes, $coff + 20 + 24)
    $secStart = $coff + 20 + $optSize
    Write-Output ("ImageBase=0x{0:X}" -f $imageBase)

    # Compute section list and section-by-fileoff
    $sections = @()
    for ($i = 0; $i -lt $numSec; $i++) {
        $s = $secStart + $i * 40
        $sections += [pscustomobject]@{
            Name        = [System.Text.Encoding]::ASCII.GetString($bytes, $s, 8).TrimEnd([char]0)
            VAddr       = [System.BitConverter]::ToUInt32($bytes, $s + 12)
            VSize       = [System.BitConverter]::ToUInt32($bytes, $s + 8)
            FileOff     = [System.BitConverter]::ToUInt32($bytes, $s + 20)
            FileSize    = [System.BitConverter]::ToUInt32($bytes, $s + 16)
        }
    }

    function SectionForFile([long]$f) {
        foreach ($s in $sections) {
            if ($f -ge $s.FileOff -and $f -lt ($s.FileOff + $s.FileSize)) { return $s.Name }
        }
        return '(none)'
    }
    function VAForFile([long]$f) {
        foreach ($s in $sections) {
            if ($f -ge $s.FileOff -and $f -lt ($s.FileOff + $s.FileSize)) {
                return $imageBase + ($s.VAddr + ($f - $s.FileOff))
            }
        }
        return 0
    }

    # Compute VAs for each target
    foreach ($name in $targets.Keys) {
        $t = $targets[$name]
        $t.va = VAForFile $t.fileOff
        Write-Output ("Target: {0,-30} file=0x{1:X} VA=0x{2:X}" -f $name, $t.fileOff, $t.va)
    }

    foreach ($name in $targets.Keys) {
        $t = $targets[$name]
        $va = [int64]$t.va
        $rva = [uint32]($va - $imageBase)

        $needle8 = New-Object byte[] 8
        for ($k = 0; $k -lt 8; $k++) { $needle8[$k] = [byte](($va -shr (8 * $k)) -band 0xFF) }
        $needle4 = New-Object byte[] 4
        for ($k = 0; $k -lt 4; $k++) { $needle4[$k] = [byte](($rva -shr (8 * $k)) -band 0xFF) }

        $hits8 = [Scanner]::FindBytes($bytes, $needle8, 20)
        $hits4 = [Scanner]::FindBytes($bytes, $needle4, 50)

        Write-Output ""
        Write-Output ("--- {0} ---" -f $name)
        Write-Output ("  8-byte VA matches: {0}" -f $hits8.Count)
        foreach ($h in $hits8) {
            $sec = SectionForFile $h
            $matchVa = VAForFile $h
            Write-Output ("    file=0x{0:X8}  VA=0x{1:X}  section={2}" -f $h, $matchVa, $sec)
        }
        Write-Output ("  4-byte RVA matches: {0}" -f $hits4.Count)
        foreach ($h in $hits4) {
            $sec = SectionForFile $h
            $matchVa = VAForFile $h
            # If this RVA match is the disp32 of a RIP-relative instruction, the next-insn VA is matchVa+4
            # and the disp32 would resolve to (matchVa + 4) + rva. Only flag as "looks like xref" if the
            # match is inside .text/.rdata (excluding the string itself).
            $isHere = ($h -eq $t.fileOff -or (($matchVa -ge ($va - 8)) -and ($matchVa -lt ($va + 8))))
            $tag = if ($isHere) { '[self]' } else { '' }
            Write-Output ("    file=0x{0:X8}  VA=0x{1:X}  section={2}  {3}" -f $h, $matchVa, $sec, $tag)
        }
    }
}

$wHGame = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll'
$wHGameArm = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGameArm.dll'

Scan-Dll -path $wHGame -targets @{
    'cant_change_outfit_in_combat' = @{ fileOff = 0x3EDDC60 }
    'cant_use_in_combat'           = @{ fileOff = 0x3EDD000 }
}
Scan-Dll -path $wHGameArm -targets @{
    'cant_change_outfit_in_combat' = @{ fileOff = 0x4EF9DA0 }
    'cant_use_in_combat'           = @{ fileOff = 0x4EF8D10 }
}
