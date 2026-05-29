"""Disassemble CSystem::Init around its ModManager_ctor call site to identify:
  - the EXACT outResult slot (a stack local? a CSystem field?)
  - what CSystem::Init does AFTER the ctor returns — does it WRITE the heap
    ptr into a singleton/global, or store it into a CSystem member?
  - whether there's a "register modMgr" call kcdx must replicate

Ctor call site is at VA 0x1807A76FE (from the recon doc + seed prose for
id 3101). Disassemble 0x80 bytes before + 0x100 bytes after.
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

CTOR_CALL_VA = 0x1807A76FE   # CSystem::Init's call to ModManager_ctor
CTOR_CALL_RVA = CTOR_CALL_VA - 0x180000000

pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va_rva = text.VirtualAddress

# Window: 0xA0 bytes before, 0x120 bytes after.
LO = max(0, CTOR_CALL_RVA - text_va_rva - 0xA0)
HI = min(len(text_data), CTOR_CALL_RVA - text_va_rva + 0x120)
window = text_data[LO:HI]
window_va = image_base + text_va_rva + LO

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True

print(f"CSystem::Init around ModManager_ctor call (VA {CTOR_CALL_VA:#x}):")
print()
for ins in md.disasm(window, window_va):
    marker = ""
    if ins.address == CTOR_CALL_VA:
        marker = "  <-- CALL TO ModManager_ctor"
    print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<10} {ins.op_str}{marker}")
