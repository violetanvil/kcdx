"""Dump auxiliary functions for the dispatcher trace."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

TARGETS = {
    "iter_neq_end@1803a4820":   0x3a4820,
    "logger_setup@1804d455c":   0x4d455c,
    "logger_warn@182475df8":    0x2475df8,
    "logger_X@182476974":       0x2476974,
}
DUMP_BYTES = 0x80

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
for label, rva in TARGETS.items():
    off = rva - text_va
    if off < 0 or off+DUMP_BYTES > len(data):
        print(f"== {label} OUT OF RANGE"); continue
    print("=" * 78); print(f"== {label} VA={base+rva:#x}"); print("=" * 78)
    for ins in md.disasm(bytes(data[off:off+DUMP_BYTES]), base+rva):
        print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
        if ins.mnemonic == "ret" and (ins.address - (base+rva)) > 0x10:
            break
    print()
