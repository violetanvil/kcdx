"""Disassemble the post-ctor "install" helper at FUN_1819DDCA4.

CSystem::Init at 0x1807A7706..0x1807A7709 does:
   mov rdx, rax            ; rdx = heap modMgr ptr (ctor return)
   mov rcx, r13            ; rcx = &(CSystem + 0x2B30)
   call FUN_1819DDCA4

Q: Does FUN_1819DDCA4 actually install the heap ptr into CSystem+0x2B30,
or does it do something else (refcount increment, conditional-replace, etc)?
And: does it ALSO publish to a global singleton (the gEnv-style at
RVA 0x0492B8A8)?
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

HELPER_VA = 0x1819DDCA4
HELPER_RVA = HELPER_VA - 0x180000000

pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")

# Sanity: HELPER_RVA must be inside .text.
text_lo = text.VirtualAddress
text_hi = text_lo + text.Misc_VirtualSize
print(f"HELPER_RVA {HELPER_RVA:#x}  .text [{text_lo:#x}, {text_hi:#x})  in_text={text_lo <= HELPER_RVA < text_hi}")

off_in_text = HELPER_RVA - text_lo
body = text.get_data()[off_in_text : off_in_text + 0x180]

md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
print()
print(f"FUN_1819DDCA4 — disasm 0x180 bytes:")
print()
ret_count = 0
for ins in md.disasm(body, HELPER_VA):
    print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<10} {ins.op_str}")
    if ins.mnemonic == "ret":
        ret_count += 1
        if ret_count >= 2:
            break
    if ins.address - HELPER_VA > 0x160:
        break
