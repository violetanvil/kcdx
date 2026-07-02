"""Locate the function that CONSTRUCTS CCryPak and STORES the pointer into gEnv+0x50.

Established so far (read in bodies):
  - CCryPak ctor      = FUN @ 0x180D2A570 (vtable LEA at 0x180D2A592, `mov [rsi],vtable`).
  - CCryPak dtor      = FUN @ 0x182415ED8 (other vtable ref; frees members) — not it.
  - NO direct `mov [rip+gEnv+0x50], reg` exists in .text (0 hits).
  - 2 sites do `lea reg, [rip+gEnvbase]` (0x18492B800): 0x180A598A1, 0x181DCAF8E.

So the store is either `lea reg,[gEnvbase]; mov [reg+0x50], pcrypak`, or the gEnv-base is
held some other way. This scan:
  (1) finds every direct CALL to the CCryPak ctor 0x180D2A570 — the construction sites;
  (2) dumps a window around each gEnv-base LEA to see if a `[reg+0x50]` store follows;
  (3) for each ctor caller, dumps a window to see whether the returned `this` is stored
      into gEnv+0x50 (via a base+0x50 write) right after the ctor call.
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
IMAGE_BASE = 0x180000000

CTOR_VA     = 0x180D2A570
GENV_VA     = IMAGE_BASE + 0x0492B800      # 0x18492B800
PCRYPAK_VA  = GENV_VA + 0x50               # 0x18492B850
VTABLE_VA   = IMAGE_BASE + 0x03A95FA8

pe = pefile.PE(DLL, fast_load=True)
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_rva = text.VirtualAddress
text_va = IMAGE_BASE + text_rva

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

ctor_callers = []
for ins in md.disasm(text_data, text_va):
    if ins.id == 0:
        continue
    if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == X86_OP_IMM:
        if ins.operands[0].imm == CTOR_VA:
            ctor_callers.append(ins.address)

print("=" * 78)
print(f"Direct callers of CCryPak ctor {CTOR_VA:#x}: {len(ctor_callers)}")
print("=" * 78)
for va in ctor_callers:
    print(f"  call site {va:#x}")
print()

def dump_window(center_va, back, fwd, label=""):
    start = center_va - back
    off = start - text_va
    win = text_data[off: off + back + fwd]
    print("-" * 78)
    print(f"WINDOW around {center_va:#x}  {label}")
    print("-" * 78)
    for ins in md.disasm(win, start):
        if ins.id == 0:
            continue
        note = ""
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if t == VTABLE_VA:   note = "   <<< CCryPak vtable"
                elif t == PCRYPAK_VA: note = "   <<< gEnv+0x50 pCryPak slot"
                elif t == GENV_VA:    note = "   <<< gEnv base"
        if ins.mnemonic == "call" and ins.operands and ins.operands[0].type == X86_OP_IMM:
            if ins.operands[0].imm == CTOR_VA:
                note += "   <<< CALL CCryPak ctor"
        # also flag any [reg+0x50] store (the indirect gEnv+0x50 write shape)
        if ins.mnemonic.startswith("mov") and "+ 0x50]" in ins.op_str and ins.op_str.split(",")[0].strip().endswith("]"):
            note += "   <<< store to [reg+0x50]"
        mark = "  *HERE*" if ins.address == center_va else ""
        print(f"  {ins.address:#012x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}{mark}")
    print()

for va in ctor_callers:
    dump_window(va, 0x60, 0x90, "(CCryPak ctor call site — does the result go to gEnv+0x50?)")

dump_window(0x0180A598A1, 0x20, 0x60, "(gEnv-base LEA #1)")
dump_window(0x0181DCAF8E, 0x20, 0x60, "(gEnv-base LEA #2)")
