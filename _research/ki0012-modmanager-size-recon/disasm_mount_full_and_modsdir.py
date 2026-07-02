"""KI-0012 — read MOUNT's REAL driver body fully (0x4d9088 thunk -> 0x4d9b1c
inner; and the 0x4d9110 per-record path) for any modMgr +0x48/+0x50/+0x58
read/write, and confirm the modsDir CryString helper FUN_1804fd468 the kcdx
bracket calls.

Also: dump the genuine object's structural truth — disassemble the helper
FUN_1804fd468 (CryString placement-construct) that the ctor calls at +0x10, to
verify kcdx's CryStrPlacementFn_t usage is ABI-correct (the modsDir nRefs=2 vs
genuine nRefs=1 PROBE-L divergence).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True


def disasm(label, rva, n):
    off = rva - text_va
    print("=" * 78)
    print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}")
    print("=" * 78)
    if off < 0 or off + n > len(text_data):
        print("   OUT OF .text"); print(); return
    for ins in md.disasm(text_data[off:off + n], base + rva):
        note = ""
        if "rip" in ins.op_str and ins.mnemonic in ("lea", "mov", "call", "cmp"):
            try:
                d = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                note = f"   -> RVA {(ins.address+ins.size+disp)-base:#x}"
            except Exception:
                pass
        print(f"  {ins.address:#010x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}")
    print()


# MOUNT real inner driver (the 0x4d9088 entry is the real driver; 0x4d9058 is a
# 1-line thunk into 0x4d9b1c). Read the per-record loop fully.
disasm("MOUNT inner 0x4d9b1c (the real driver — OpenPacks loop over enabled list)", 0x4d9b1c, 0x200)

# The modsDir CryString placement-construct helper the ctor calls at +0x10.
disasm("CryString_placement_construct FUN_1804fd468 (ctor +0x10 builder)", 0x4fd468, 0xC0)
