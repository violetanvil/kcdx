"""Find indirect calls of the form `call qword ptr [reg + 0x100]` or `[reg + 0x108]`.

These are likely the engine-side IConsole::AddCommand call sites.

Encoding pattern for `call qword ptr [reg + disp32]`:
  FF /2 [mod=10 reg=010 rm=reg]
  i.e.   FF 90+rm  disp32     -- regs RAX..RDI without SIB
For r8..r15: REX.B = 41
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
TARGET_DISPS = [0x100, 0x108]  # vtable slots 32 and 33

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data(); text_va = text.VirtualAddress
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
    n = len(data)

    hits = {d: [] for d in TARGET_DISPS}

    # Match call [reg + disp32] for both prefixed and unprefixed forms.
    # Without REX:  FF 90/91/92/93/94/95/96/97   disp32   (RAX/RCX/RDX/RBX/SIB?/RBP/RSI/RDI)
    #   - rm=4 (RSP) needs SIB byte, skip.
    #   - rm=5 (RBP) uses [RBP+disp32] form (no special encoding).
    # With REX.B (41): FF 90..97 disp32 for r8..r15.
    # We just check that opcode bytes are (FF, modrm_first_nibble=9, modrm_low=0..7 except 4 SIB).
    for i in range(n - 6):
        b0 = data[i]
        # Two forms: no REX, or REX with R/B/W set on reg.
        if b0 == 0xFF:
            modrm = data[i + 1]
            disp = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
            instr_va = base + text_va + i
            instr_bytes = bytes(data[i:i + 6])
        elif b0 in (0x41, 0x44, 0x45, 0x4D, 0x4C, 0x48, 0x49, 0x4A):
            if data[i + 1] != 0xFF: continue
            modrm = data[i + 2]
            disp = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
            instr_va = base + text_va + i
            instr_bytes = bytes(data[i:i + 7])
        else:
            continue
        # /2 means modrm.reg = 010. Mod=10 means disp32 follows. RM=4 is SIB; skip.
        mod = (modrm >> 6) & 3
        reg = (modrm >> 3) & 7
        rm = modrm & 7
        if mod != 2 or reg != 2: continue
        if rm == 4: continue
        if disp in TARGET_DISPS:
            hits[disp].append((instr_va, instr_bytes))

    for d in TARGET_DISPS:
        lst = hits[d]
        print(f"\n=== call qword ptr [reg+{d:#x}]   ({len(lst)} hits)")
        for va, b in lst[:50]:
            i = va - base - text_va
            start = max(0, i - 32); end = min(n, i + 16)
            print(f"  --- at {va:#x}")
            for ins in md.disasm(bytes(data[start:end]), base + text_va + start):
                marker = "  *" if ins.address == va else "   "
                print(f"  {marker} {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
        if len(lst) > 50:
            print(f"  ... ({len(lst) - 50} more)")

if __name__ == "__main__":
    main()
