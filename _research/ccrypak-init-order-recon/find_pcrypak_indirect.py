"""Follow-up: the direct `mov [rip+slot], reg` scan found ZERO writes to gEnv+0x50.
So the store is INDIRECT. Two ways the engine reaches the slot to store into it:
  (A) `lea reg, [rip+slot]` then later `mov [reg], pcrypak`  — load the slot ADDRESS.
  (B) write through a gEnv BASE pointer held in a register: `lea reg,[rip+gEnv]` /
      `mov reg,[rip+pGenvBase]` then `mov [reg+0x50], pcrypak`.
This scan finds (A) — every `lea reg,[rip+target]` whose target == the slot VA (or the
gEnv base VA, since a store through gEnv-base+0x50 starts by loading the gEnv base).
It also dumps the two functions that LEA the CCryPak vtable (the construction tell),
so we can read the ctor body and the pointer's flow into the slot.
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
IMAGE_BASE = 0x180000000

GENV_RVA    = 0x0492B800
GENV_VA     = IMAGE_BASE + GENV_RVA       # 0x18492B800
PCRYPAK_VA  = IMAGE_BASE + GENV_RVA + 0x50 # 0x18492B850
VTABLE_VA   = IMAGE_BASE + 0x03A95FA8      # 0x183A95FA8

pe = pefile.PE(DLL, fast_load=True)
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_rva = text.VirtualAddress
text_va = IMAGE_BASE + text_rva

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

lea_slot = []     # lea reg, [rip+slot]  — loads the slot address (precursor to indirect store)
lea_genv = []     # lea reg, [rip+gEnvbase] — loads gEnv base (store through base+0x50)
mov_genv_base = []  # mov reg, [rip+somePtr] where target is the gEnv base VA (rare)

for ins in md.disasm(text_data, text_va):
    if ins.id == 0:
        continue
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            tgt = ins.address + ins.size + op.mem.disp
            if ins.mnemonic == "lea":
                if tgt == PCRYPAK_VA:
                    lea_slot.append((ins.address, ins.op_str))
                elif tgt == GENV_VA:
                    lea_genv.append((ins.address, ins.op_str))

print("=" * 78)
print("Indirect-store leads for gEnv+0x50 (pCryPak)")
print(f"  slot VA = {PCRYPAK_VA:#x}   gEnv base VA = {GENV_VA:#x}   vtable VA = {VTABLE_VA:#x}")
print("=" * 78)
print()
print(f"`lea reg,[rip+slot]` (loads pCryPak slot ADDRESS — precursor to an indirect store): {len(lea_slot)}")
for va, ops in lea_slot:
    print(f"  {va:#012x}  lea {ops}")
print()
print(f"`lea reg,[rip+gEnvbase]` (loads gEnv base — a store would be [reg+0x50]): {len(lea_genv)}")
for va, ops in lea_genv:
    print(f"  {va:#012x}  lea {ops}")
print()

# Dump bodies around the two CCryPak-vtable LEA sites (the constructor tell). Walk back
# to a plausible function start (look for the standard `mov [rsp+8],rcx`/`push`/`sub rsp`
# prologue scan upward is hard from mid-stream; instead disassemble a window centered on
# each site so we can read the vtable store + nearby pointer flow).
def dump_window(center_va, back=0x80, fwd=0xC0, label=""):
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
                if t == VTABLE_VA:
                    note = "   <<< CCryPak vtable"
                elif t == PCRYPAK_VA:
                    note = "   <<< gEnv+0x50 pCryPak slot"
                elif t == GENV_VA:
                    note = "   <<< gEnv base"
        mark = "  *HERE*" if ins.address == center_va else ""
        print(f"  {ins.address:#012x}  {ins.bytes.hex():<20} {ins.mnemonic:<7} {ins.op_str}{note}{mark}")
    print()

dump_window(0x0180D2A592, label="(vtable LEA #1 — candidate CCryPak ctor)")
dump_window(0x0182415EF6, label="(vtable LEA #2)")
