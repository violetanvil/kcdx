"""Read the CALLER frame (the real outer loop) + resolve the two polled globals.
Caller return site = CreateInstance+0x2e8c63. cdb resolved CreateInstance's base;
we get it from the export/symbol via the offset chain. We disasm AROUND the
caller return address and resolve the rip-relative globals at 0x40c5841 / 0x40c5827
referenced from the inner function (absolute = next_insn_rva + disp)."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
def rva_to_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
IB = 0x180000000

# Resolve the two polled globals (rip-relative from the inner fn).
# 0x866048: mov rcx,[rip+0x40c5841]; next insn at 0x86604f -> g1 = 0x86604f + 0x40c5841
# 0x866062: mov rcx,[rip+0x40c5827]; next insn at 0x866069 -> g2 = 0x866069 + 0x40c5827
g1 = 0x86604f + 0x40c5841
g2 = 0x866069 + 0x40c5827
# the loop's first call [rip+0x319d237] at 0x866023, next 0x866029
c1 = 0x866029 + 0x319d237
# Sleep call [rip+0x319c6a8] at 0x86608a, next 0x866090
csleep = 0x866090 + 0x319c6a8
print(f"polled global g1 (0x40c5841 rel) = RVA 0x{g1:x}")
print(f"polled global g2 (0x40c5827 rel) = RVA 0x{g2:x}")
print(f"loop-top call target ptr (0x319d237 rel) = RVA 0x{c1:x}")
print(f"Sleep call target ptr (0x319c6a8 rel) = RVA 0x{csleep:x}  (expect a Sleep import thunk)")

# Read what those IAT/global slots point at (8 bytes each, if in a data section).
for name, rva in [("g1",g1),("g2",g2),("loop_call_ptr",c1),("sleep_call_ptr",csleep)]:
    off = rva_to_off(rva)
    if off is None:
        print(f"{name}: RVA 0x{rva:x} not mapped"); continue
    raw = pe.get_data(rva, 8)
    val = int.from_bytes(raw, "little")
    print(f"{name} @ RVA 0x{rva:x}: 8 bytes = 0x{val:016x}")

# Is sleep_call_ptr an import? Resolve via import table.
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        if imp.address and (imp.address - IB) in (csleep, c1):
            print(f"  IMPORT at 0x{imp.address-IB:x} = {entry.dll.decode()}!{imp.name.decode() if imp.name else imp.ordinal}")
