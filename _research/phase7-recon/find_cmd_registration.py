"""Find xrefs to known engine command-name strings.

Looking for engine-side IConsole::AddCommand registrations.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
TARGETS = {
    "playerGoto":  0x183e11658,
    "freeze":      0x183e06288,
    "sys_spec":    0x183a7e620,
    "kill":        0x183b32bf0,
}

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data(); text_va = text.VirtualAddress
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
    n = len(data)
    found = {v: [] for v in TARGETS.values()}
    for i in range(n - 7):
        b0 = data[i]
        if b0 not in (0x48, 0x4C): continue
        if data[i + 1] != 0x8D: continue
        modrm = data[i + 2]
        if (modrm >> 6) != 0 or (modrm & 7) != 5: continue
        disp32 = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
        target = base + text_va + i + 7 + disp32
        if target in found:
            found[target].append(base + text_va + i)

    for label, va in TARGETS.items():
        lst = found[va]
        print(f"\n=== {label} {va:#x}  ({len(lst)} xrefs) ===")
        for v in lst[:6]:
            i = v - base - text_va
            start = max(0, i - 32); end = min(n, i + 80)
            print(f"  --- at {v:#x}")
            for ins in md.disasm(bytes(data[start:end]), base + text_va + start):
                marker = "  *" if ins.address == v else "   "
                print(f"  {marker} {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
            print()

if __name__ == "__main__":
    main()
