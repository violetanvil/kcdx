"""Find engine-side calls to AddCommand (slot 32 OR 33) of IConsole.

The pattern we want: any indirect call of the form
    call qword ptr [rax + 0x100]   ; slot 32 = +0x100
    call qword ptr [rax + 0x108]   ; slot 33 = +0x108
Or any direct call to the addresses 0x18100A3D4 / 0x180B9A2B0.

Direct calls are easier (rel32 relative to next-instruction). E8 disp32.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

TARGETS = {
    "AddCommand_scriptString (slot 32)": 0x18100A3D4,
    "AddCommand_funcOverload (slot 33)": 0x180B9A2B0,
}

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data(); text_va = text.VirtualAddress
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True

    n = len(data)
    found = {v: [] for v in TARGETS.values()}
    # Scan for E8 disp32 direct calls.
    for i in range(n - 5):
        if data[i] != 0xE8: continue
        disp32 = int.from_bytes(data[i+1:i+5], "little", signed=True)
        target = base + text_va + i + 5 + disp32
        if target in found:
            found[target].append(base + text_va + i)
    # Scan for indirect: pattern  FF 90 / FF 50 (call [reg+disp]); harder to interpret.
    for label, va in TARGETS.items():
        lst = found[va]
        print(f"\n=== Direct callers of {label} {va:#x}  ({len(lst)}) ===")
        for v in lst[:40]:
            print(f"  {v:#012x}")

if __name__ == "__main__":
    main()
