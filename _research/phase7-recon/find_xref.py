"""Find LEA xrefs in .text for specific .rdata string VAs."""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

TARGETS = [
    0x18473f3b0,  # "Something called the '%s' command with the wrong number of arguments"
    0x183dcf870,  # "Unknown command: %s"
    0x183dcf888,  # "[CONSOLE] Executing console command '%s'"
    0x183dcf790,  # "[CVARS]: [DUPLICATE] CXConsole::AddCommand(): console command [%s] is already registered"
    0x183dcf7f0,  # "[CVARS]: [DUPLICATE] CXConsole::AddCommand(): script command [%s] is already registered"
    0x183b70640,  # 'ExecuteString'
    0x18473f4f0,  # near "wrong number of arguments"; will probe
    0x1840871f0,  # 'wrong number of arguments'
    0x183dcf8c0,  # '[CVARS]: $3[FAIL] [%s] = $6[%s] $4(expected [%s] in group [%s] = [%s])'
    0x1840b5618,  # 'XNetwork - Function not implemented' (sanity)
]

CONTEXT_BYTES = 80


def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data()
    text_va = text.VirtualAddress

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.skipdata = True

    # For each target, scan .text for LEA r,[rip+disp] that resolves to it.
    # An LEA instruction is 7 bytes: 48 8D ?? ?? ?? ?? ??  (REX.W + 8D + ModRM + disp32)
    # The simplest way: linear disassemble and check operand strings.
    # But that's slow on a 60MB .text. Instead: regex-match the encoded LEA forms.
    # LEA reg, [rip+disp32] encoding:
    #   48 8D /r  ModRM with mod=00 rm=101 (RIP-relative); reg = chosen reg (0..7 or with REX.R extension)
    # We need to scan for any 7-byte LEA with the right disp.

    found = {t: [] for t in TARGETS}

    n = len(data)
    # Pre-build the set of expected (rip-after-instr, disp32) pairs.
    # disp32 = target - (rip_after_instr)
    # rip_after_instr = (text_va + i + 7) + base
    # So: disp32 = target - (text_va + i + 7) - base
    # We scan all positions where byte[i] == 0x48 and byte[i+1] == 0x8D and ModRM(byte[i+2]) has mod=00 rm=101.

    # Build target list and compute, for each candidate i: which target matches?
    # We'll just iterate over `i` from 0..n-7 and check.

    for i in range(0, n - 7):
        if data[i] != 0x48:
            # Also check REX with R extension: 0x4C 0x8D /r
            if data[i] != 0x4C:
                continue
        if data[i + 1] != 0x8D:
            continue
        modrm = data[i + 2]
        mod = modrm >> 6
        rm = modrm & 7
        if mod != 0 or rm != 5:
            continue
        disp32 = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
        rip_after = base + text_va + i + 7
        target = rip_after + disp32
        if target in found:
            ins_va = base + text_va + i
            found[target].append(ins_va)

    md_local = Cs(CS_ARCH_X86, CS_MODE_64); md_local.skipdata = True
    for t, list_ in found.items():
        print(f"\n==== target {t:#x}  ({len(list_)} xref(s))")
        for va in list_[:20]:
            # Disassemble 16 bytes preceding and 32 bytes following for context.
            i = va - (base + text_va)
            start = max(0, i - 16)
            end = min(n, i + 32)
            ctx_va = base + text_va + start
            print(f"  xref at VA {va:#x}:")
            for ins in md_local.disasm(bytes(data[start:end]), ctx_va):
                marker = "  *" if ins.address == va else "   "
                print(f"  {marker} {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
        if len(list_) > 20:
            print(f"  ... ({len(list_) - 20} more)")


if __name__ == "__main__":
    main()
