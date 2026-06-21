"""Find the OUTER caller of the window-poll fn (0x865fb4) and read whether it
loops. The caller return site is CreateInstance+0x2e8c63. We don't have
CreateInstance's base symbol statically, but the return ADDRESS on the stack was
an absolute VA; instead, scan .text for a `call` whose target == 0x865fb4 (E8 rel32),
then disasm around each call site looking for a back-edge (a jmp/jcc to before the call)."""
import pefile, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
IB = 0x180000000
TARGET = 0x865fb4
text = None
for s in pe.sections:
    if b".text" in s.Name:
        text = s; break
data = text.get_data()
base_rva = text.VirtualAddress
calls = []
# E8 rel32 call: target = insn_rva + 5 + rel32
for i in range(len(data)-5):
    if data[i] == 0xE8:
        rel = struct.unpack("<i", data[i+1:i+5])[0]
        site_rva = base_rva + i
        tgt = site_rva + 5 + rel
        if tgt == TARGET:
            calls.append(site_rva)
print(f"call sites targeting 0x{TARGET:x}: {[hex(c) for c in calls]}")
# Disasm a window around each call site to see the surrounding loop structure.
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
for site in calls:
    print(f"\n=== around call site 0x{site:x} (window -0x60..+0x40) ===")
    start = site - 0x60
    off = None
    for s in pe.sections:
        if s.VirtualAddress <= start < s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            off = s.PointerToRawData+(start-s.VirtualAddress)
    chunk = pe.get_data(start, 0xA0)
    for insn in md.disasm(chunk, IB+start):
        rva = insn.address - IB
        mark = "CALL>>>" if rva==site else "   "
        # flag backward jumps (potential outer loop back-edge)
        bj = ""
        if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
            try:
                t=int(insn.op_str,16)-IB
                if t < rva: bj="  <-- BACK-EDGE (outer loop?)"
            except: pass
        print(f"{mark} 0x{rva:08x}  {insn.mnemonic:<8}{insn.op_str}{bj}")
