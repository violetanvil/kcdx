"""Whole-.text scan: find every instruction that WRITES gEnv+0x50 (the pCryPak slot).

Question (one verifiable address): WHERE is the constructed CCryPak pointer STORED
into gEnv+0x50 (gEnv_pCryPak, the gEnv_pCryPak slot)? The prior recon proved CSystem::Init
only READS gEnv+0x50 (at VA 0x1807A7225) and never writes it in its own body — so the
construct+store is in an earlier callee or a function that runs before Init.

Method (AP19-clean — the write edge is read in the binary, not inferred):
  - The slot VA is fixed: gEnv base RVA 0x0492B800 + 0x50 = RVA 0x0492B850
    → VA 0x18492B850 (image base 0x180000000).
  - Scan ALL of .text with capstone. For every `mov [rip+disp32], reg` (a RIP-relative
    STORE) whose computed RIP-relative target == the slot VA, that is a write of the
    pCryPak pointer into the slot. (The READ form `mov reg, [rip+disp]` is the dest=reg
    case and is excluded — we want dest=[mem].)
  - Also flag any RIP-relative READ of the slot, as a cross-check that the resolver is
    correct (the known read at 0x1807A7225 must appear).

Anchors (all from data/db-export seeds, NOT invented):
  gEnv base RVA        = 0x0492B800   [versions seed id 11 / id 1010 note: "gEnv (id 1010, 0x0492B800)"]
  gEnv_pCryPak slot    = 0x0492B850 = gEnv+0x50   [names seed id 132: "DAT_18492b850 ... == gEnv+0x50"]
  CCryPak vtable RVA   = 0x03A95FA8   [names seed id 131/132: "*pCryPak is the CCryPak vtable (RVA 0x03A95FA8)"]
  known READ site      = VA 0x1807A7225 (mov rcx,[rip+0x4184624])  [prior recon _csysinit_fs_order.txt]
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
IMAGE_BASE = 0x180000000

GENV_RVA      = 0x0492B800
PCRYPAK_RVA   = GENV_RVA + 0x50          # 0x0492B850
PCRYPAK_VA    = IMAGE_BASE + PCRYPAK_RVA  # 0x18492B850
VTABLE_RVA    = 0x03A95FA8
VTABLE_VA     = IMAGE_BASE + VTABLE_RVA   # 0x183A95FA8

pe = pefile.PE(DLL, fast_load=True)
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_rva = text.VirtualAddress
text_va = IMAGE_BASE + text_rva

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

writes = []   # (va, mnemonic, op_str, src_reg)
reads = []    # (va, mnemonic, op_str)
vtable_lea = []  # any LEA/MOV whose RIP target == the CCryPak vtable VA (construction tell)

for ins in md.disasm(text_data, text_va):
    if ins.id == 0:
        continue
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            tgt = (ins.address + ins.size + op.mem.disp)
            # dest operand is [mem] ⇒ a STORE; the op_str's first operand ends in ']'
            first_op = ins.op_str.split(",")[0].strip()
            dest_is_mem = first_op.endswith("]")
            if tgt == PCRYPAK_VA:
                if dest_is_mem and ins.mnemonic.startswith("mov"):
                    src = ins.op_str.split(",", 1)[1].strip() if "," in ins.op_str else "?"
                    writes.append((ins.address, ins.mnemonic, ins.op_str, src))
                else:
                    reads.append((ins.address, ins.mnemonic, ins.op_str))
            if tgt == VTABLE_VA:
                vtable_lea.append((ins.address, ins.mnemonic, ins.op_str))

print("=" * 78)
print("Whole-.text scan for gEnv+0x50 (pCryPak slot) accesses")
print(f"  slot RVA = {PCRYPAK_RVA:#08x}   slot VA = {PCRYPAK_VA:#x}")
print(f"  CCryPak vtable VA = {VTABLE_VA:#x} (RVA {VTABLE_RVA:#08x})")
print("=" * 78)
print()
print(f"WRITES to gEnv+0x50 (mov [rip+slot], reg): {len(writes)}")
for va, mn, ops, src in writes:
    print(f"  {va:#012x}  {mn} {ops}   (src reg = {src})")
print()
print(f"READS of gEnv+0x50 (cross-check; known read @ 0x1807A7225 must appear): {len(reads)}")
for va, mn, ops in reads:
    print(f"  {va:#012x}  {mn} {ops}")
print()
print(f"RIP-relative refs to the CCryPak vtable VA {VTABLE_VA:#x}: {len(vtable_lea)}")
for va, mn, ops in vtable_lea:
    print(f"  {va:#012x}  {mn} {ops}")
