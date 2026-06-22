"""KI-0028 DIVERGENCE-A — this-provenance-verified callers of metadata slots 45/67/70.

A genuine `gEnv->pCryPak->IsFileExist/GetFileSize(...)` site is:
  mov  reg, [rip+disp]          ; reg <- *0x18492B850  (the pCryPak global)
  ... (short window, reg not clobbered) ...
  mov  rcx, reg / lea ...        ; this = pCryPak  (rcx for fastcall)
  mov  rax, [reg]                ; rax <- vtable
  call qword ptr [rax + 0xNNN]   ; slot

We approximate provenance by: find every `mov reg64, [rip+d]` where rip-target == PCRYPAK,
then within the next ~64 bytes look for a `call qword ptr [r2 + slotoff]` where r2 was loaded
from [reg] (vtable load `mov r2,[reg]`). Report the call-site + enclosing func.

Slots: 45 GetFileSize +0x168 ; 67 IsFileExist3 +0x218 ; 70 IsFileExist2 +0x230.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
PCRYPAK = 0x18492B850
SLOTS = {0x168: "slot45_GetFileSize", 0x218: "slot67_IsFileExist3", 0x230: "slot70_IsFileExist2"}

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress
text_start = base + tva
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

# 1) all instructions, linear from .text start (good enough; we re-disasm windows)
# Build a quick index: VA -> (mnemonic, op_str, size, detail) by disassembling whole .text.
insns = list(md.disasm(data, text_start))
by_va = {}
order = []
for ins in insns:
    by_va[ins.address] = ins
    order.append(ins.address)

# map: which RIP-relative mem loads target PCRYPAK -> the dest reg
def rip_target(ins):
    # ins with a RIP-relative mem operand: target = next_ins_va + disp
    for op in ins.operands:
        if op.type == 3 and op.mem.base == md.reg_name(0) is None:
            pass
    return None

from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_REG_RIP

def get_load_pcrypak(ins):
    """return dest reg id if ins is `mov reg, [rip+d]` with rip-target == PCRYPAK"""
    if ins.mnemonic != "mov":
        return None
    if len(ins.operands) != 2:
        return None
    d, s = ins.operands
    if d.type != X86_OP_REG or s.type != X86_OP_MEM:
        return None
    if s.mem.base != X86_REG_RIP:
        return None
    target = ins.address + ins.size + s.mem.disp
    if target == PCRYPAK:
        return d.reg
    return None

def is_vtable_load(ins):
    """`mov r2, [r1]` (disp 0) -> (r2, r1)"""
    if ins.mnemonic != "mov" or len(ins.operands) != 2:
        return None
    d, s = ins.operands
    if d.type != X86_OP_REG or s.type != X86_OP_MEM:
        return None
    if s.mem.base != 0 and s.mem.index == 0 and s.mem.disp == 0:
        return (d.reg, s.mem.base)
    return None

def is_slot_call(ins):
    """`call qword ptr [r + slotoff]` -> (basereg, slotoff)"""
    if ins.mnemonic != "call" or len(ins.operands) != 1:
        return None
    o = ins.operands[0]
    if o.type != X86_OP_MEM or o.mem.base == 0:
        return None
    if o.mem.disp in SLOTS:
        return (o.mem.base, o.mem.disp)
    return None

def enclosing_func_start(va):
    off = va - text_start
    j = off
    limit = max(0, off - 0x3000)
    while j > limit:
        if data[j] == 0xCC and j+1 <= off and data[j+1] != 0xCC:
            return text_start + j + 1
        j -= 1
    return None

# walk: for each pcrypak-load, track the reg; scan forward up to WINDOW insns for a
# vtable-load from that reg, then a slot-call on the vtable reg.
WINDOW = 24
results = []  # (slotname, callva, funcstart)
for idx, va in enumerate(order):
    ins = by_va[va]
    preg = get_load_pcrypak(ins)
    if preg is None:
        continue
    # forward scan
    vtreg = None
    for k in range(1, WINDOW):
        if idx + k >= len(order):
            break
        nins = by_va[order[idx + k]]
        # if preg is overwritten by a non-vtable mov, stop tracking (rough)
        vl = is_vtable_load(nins)
        if vl and vl[1] == preg:
            vtreg = vl[0]
        sc = is_slot_call(nins)
        if sc and vtreg is not None and sc[0] == vtreg:
            fs = enclosing_func_start(nins.address)
            results.append((SLOTS[sc[1]], nins.address, fs))
            break
        # also: direct `call [preg-vtable...]` patterns where rcx set etc. (skip)

print("=== this==pCryPak provenance-verified metadata-slot call sites ===\n")
byslot = {}
for name, callva, fs in results:
    byslot.setdefault(name, []).append((callva, fs))
for name in ("slot45_GetFileSize", "slot67_IsFileExist3", "slot70_IsFileExist2"):
    lst = byslot.get(name, [])
    print(f"--- {name}: {len(lst)} provenance-verified site(s)")
    for callva, fs in lst:
        fss = f"func {fs:#x} (RVA {fs-base:#x})" if fs else "func ?"
        print(f"    call VA {callva:#x}  RVA {callva-base:#x}   in {fss}")
    print()
print(f"TOTAL provenance-verified sites: {len(results)}")
