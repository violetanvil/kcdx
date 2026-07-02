"""Disassemble CSystem::Init's body from its START to find the ordering of the
file-system / gEnv+0x50 (pCryPak) write relative to the ModManager_ctor call.

Question (one verifiable boot-order fact): WHERE in CSystem::Init does the
CCryPak file-system object get constructed and its pointer stored into gEnv+0x50
(gEnv_pCryPak, RVA 0x0492B850 = gEnv 0x0492B800 + 0x50) — BEFORE or AFTER the
ModManager_ctor call at VA 0x1807A76FE? And are there engine file calls
(CCryPak_FOpen id 131 @ RVA 0x004614A0) between the pCryPak write and the ctor call?

Anchors (all from data/db-export seeds, NOT invented):
  CSystem::Init start  = FUN_1807a6c64 @ VA 0x1807A6C64 (RVA 0x7A6C64)  [seed id 133 prose]
  ModManager_ctor call = VA 0x1807A76FE (RVA 0x7A76FE)                  [seed id 133/134 prose]
  gEnv base RVA        = 0x0492B800   [versions seed id 11]
  gEnv_pCryPak slot    = 0x0492B850 = gEnv+0x50   [versions seed id 132]
  CCryPak_FOpen        = RVA 0x004614A0  [versions seed id 131]

This reads the actual instruction bytes — every CALL target and every gEnv-struct
memory write is read in CSystem::Init's own body (AP19: no edge inferred).
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
IMAGE_BASE = 0x180000000

INIT_START_RVA = 0x7A6C64        # CSystem::Init start
CTOR_CALL_VA   = 0x1807A76FE     # call to ModManager_ctor inside CSystem::Init
GENV_RVA       = 0x0492B800      # gEnv base
GENV_PCRYPAK   = 0x0492B850      # gEnv+0x50  (pCryPak slot)
FOPEN_RVA      = 0x004614A0      # CCryPak_FOpen
GENV_LO, GENV_HI = GENV_RVA, GENV_RVA + 0x100   # the gEnv struct window (fields)

pe = pefile.PE(DLL, fast_load=True)
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_rva = text.VirtualAddress

def rva_to_off(rva: int) -> int:
    return rva - text_rva

# Disassemble from Init start. Cap generously past the ctor call so we capture
# the whole prologue->ctor span and a bit after; stop at the first int3-padding /
# ret-then-pad that looks like the function epilogue boundary, else the cap.
START_OFF = rva_to_off(INIT_START_RVA)
CAP = 0x400 + (CTOR_CALL_VA - IMAGE_BASE - INIT_START_RVA)   # to ctor + 0x400 after
window = text_data[START_OFF:START_OFF + CAP]
start_va = IMAGE_BASE + INIT_START_RVA

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

calls = []          # (va, target_rva or None, raw)
genv_writes = []    # (va, target_rva, mnemonic, op_str, is_write)
pcrypak_touch = []  # (va, mnemonic, op_str)
fopen_calls = []    # (va,)
ctor_seen_at = None
lines = []

for ins in md.disasm(window, start_va):
    va = ins.address
    rva = va - IMAGE_BASE
    note = ""

    if ins.id == 0:  # skipdata pseudo-instruction (no operands/detail)
        lines.append(f"  {va:#012x}  {ins.bytes.hex():<22} (data)")
        continue

    # CALL — read the target (direct rel32 only; indirect noted as such)
    if ins.mnemonic == "call":
        tgt = None
        if ins.operands and ins.operands[0].type == X86_OP_IMM:
            tgt = ins.operands[0].imm - IMAGE_BASE
        calls.append((va, tgt, ins.op_str))
        if tgt is not None:
            note += f"  [CALL -> RVA {tgt:#08x}]"
            if tgt == FOPEN_RVA:
                note += "  *** CCryPak_FOpen ***"
                fopen_calls.append(va)
        else:
            note += "  [CALL indirect]"
        if va == CTOR_CALL_VA:
            note += "  <<<<< ModManager_ctor CALL SITE"
            ctor_seen_at = va

    # RIP-relative memory operand resolving into the gEnv struct
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            disp = op.mem.disp
            tgt_rva = (va + ins.size + disp) - IMAGE_BASE
            if GENV_LO <= tgt_rva < GENV_HI:
                is_write = ins.mnemonic.startswith("mov") and ins.op_str.split(",")[0].strip().endswith("]")
                genv_writes.append((va, tgt_rva, ins.mnemonic, ins.op_str, is_write))
                tag = "WRITE" if is_write else "read"
                note += f"  [gEnv+{tgt_rva-GENV_RVA:#x} {tag}]"
                if tgt_rva == GENV_PCRYPAK:
                    note += "  *** gEnv+0x50 pCryPak ***"
                    pcrypak_touch.append((va, ins.mnemonic, ins.op_str))

    lines.append(f"  {va:#012x}  {ins.bytes.hex():<22} {ins.mnemonic:<8} {ins.op_str}{note}")

# Print only the interesting summary + a slice of the body around key events.
print("=" * 78)
print(f"CSystem::Init @ {start_va:#x} (RVA {INIT_START_RVA:#x}) — file-system / pCryPak ordering")
print(f"  ctor call site = {CTOR_CALL_VA:#x};  gEnv+0x50 = RVA {GENV_PCRYPAK:#08x};  FOpen = RVA {FOPEN_RVA:#08x}")
print("=" * 78)
print()
print(f"gEnv-struct memory touches in Init body (before+after ctor): {len(genv_writes)}")
for va, tr, mn, ops, w in genv_writes:
    rel = "BEFORE ctor" if va < CTOR_CALL_VA else "AFTER ctor "
    print(f"  {va:#012x} {rel}  gEnv+{tr-GENV_RVA:#04x} {'WRITE' if w else 'read '}  {mn} {ops}")
print()
print(f"gEnv+0x50 (pCryPak) touches: {len(pcrypak_touch)}")
for va, mn, ops in pcrypak_touch:
    rel = "BEFORE ctor" if va < CTOR_CALL_VA else "AFTER ctor"
    print(f"  {va:#012x} {rel}  {mn} {ops}")
print()
print(f"CCryPak_FOpen direct calls in Init body: {len(fopen_calls)}")
for va in fopen_calls:
    rel = "BEFORE ctor" if va < CTOR_CALL_VA else "AFTER ctor"
    print(f"  {va:#012x} {rel}")
print()
print(f"Total direct CALLs before the ctor call site: "
      f"{sum(1 for va,t,_ in calls if va < CTOR_CALL_VA and t is not None)}")
print(f"ctor call site reached: {ctor_seen_at is not None} ({ctor_seen_at if ctor_seen_at else 'NOT FOUND'})")
print()
print("=" * 78)
print("FULL BODY (Init start -> ctor + a bit after):")
print("=" * 78)
for ln in lines:
    print(ln)
