"""Disassemble FUN_182440c6c — the per-mod version-gate validator.

Crash is at +0x19 (RVA 0x244D085). Read the first ~64 bytes (more than
enough for the prologue + the crashing instruction) to see what it
dereferences.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

# The function and the crash offset
FN_RVA = 0x2440C6C
CRASH_RVA = 0x244D085
CRASH_OFFSET = CRASH_RVA - FN_RVA  # = 0xC419 — wait, that's wrong
# Actually let me recompute: 0x244D085 - 0x2440C6C
# = 0x244D085
# - 0x2440C6C
# = 0xC419 ?? No:
# 0x244D085 = 38_063_237
# 0x2440C6C = 38_014_060
# delta = 49_177 = 0xC019
# Hmm. But the seed prose said "+0x19 into FUN_182440c6c" for module_rva=0x2440C85.
# Let me check: 0x2440C85 - 0x2440C6C = 0x19. Yes.
# So the crash this run is at 0x244D085 not 0x2440C85.
# Let me re-check our crash log:
#   module_rva=38014085 = 0x244C685? No:
#     38014085 / 0x244D085 — let me compute:
#     0x244D085 = 0x244_0000 + 0xD085 = 38_010_880 + 53_381 = ...
#     Hmm need to just print it.
# Wait, the SQL query above returned the row for FUN_182440c6c with rva=38014060
# (= 0x2440C6C) and length=273. The crash module_rva=38014085 satisfies
# 38014060 <= 38014085 <= 38014333 (38014060 + 273) — yes.
# delta = 38014085 - 38014060 = 25 = 0x19. So crash is +0x19, same offset as
# the absorb-doc's earlier crash.

import sys
print(f"FUN_182440c6c RVA = {FN_RVA:#x}")
print(f"CRASH RVA         = {CRASH_RVA:#x}")
print(f"CRASH OFFSET into function = {CRASH_RVA - FN_RVA} = {CRASH_RVA - FN_RVA:#x}")
print()

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress

off = FN_RVA - text_va
body = text_data[off:off + 0x80]
va = base + FN_RVA

md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
print(f"=== FUN_{FN_RVA:08x} disassembly (first 0x80 bytes) ===")
for ins in md.disasm(body, va):
    marker = " <-- CRASH" if (ins.address - base) == CRASH_RVA else ""
    print(f"  {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}{marker}")
    if (ins.address - base) > CRASH_RVA + 0x10:
        break
