#!/usr/bin/env python3
"""
Verify the 6 vtable-slot-index seed facts (ids 19-24) against WHGame.dll.

READ-ONLY. Locates the CONCRETE-class vtable for each interface in the binary,
reads the slot the seed asserts, and disassembles the target to confirm/refute
the named method. The "+1 inserted virtual" claims are confirmed/refuted against
the real contiguous vtable, never trusted (AP3).

Image base 0x180000000. KCD2 1.5.1164953.
"""
import struct, os
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

REPO = r"c:/Users/Michael/Documents/KCD2 Mods/kcdx"
DLL  = os.path.join(REPO, "third-party-ghidra", "WHGame.dll")
IMAGE_BASE = 0x180000000

pe = pefile.PE(DLL, fast_load=True)
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1")
    va = s.VirtualAddress
    vsize = max(s.Misc_VirtualSize, s.SizeOfRawData)
    is_exec = bool(s.Characteristics & 0x20000000)
    sections.append((va, va + vsize, s.PointerToRawData, name, is_exec, s.SizeOfRawData))

with open(DLL, "rb") as f:
    DATA = f.read()

def rva_to_off(rva):
    for va, end, raw, name, ex, rawsz in sections:
        if va <= rva < end:
            delta = rva - va
            if delta < rawsz:
                return raw + delta
            return None
    return None

def section_of(rva):
    for va, end, raw, name, ex, rawsz in sections:
        if va <= rva < end:
            return name, ex
    return None, False

def read(rva, n):
    off = rva_to_off(rva)
    if off is None: return None
    return DATA[off:off+n]

def qword(rva):
    b = read(rva, 8)
    return struct.unpack("<Q", b)[0] if b and len(b) == 8 else None

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def disasm(rva, n=40, count=24):
    b = read(rva, n)
    out = []
    if not b: return out
    for ins in md.disasm(b, IMAGE_BASE + rva):
        out.append((ins.address - IMAGE_BASE, ins.mnemonic, ins.op_str, ins.bytes.hex()))
        if len(out) >= count: break
    return out

def show(rva, label, n=64, count=18):
    print(f"  -- disasm {label} @ RVA {hex(rva)} --")
    for off, mn, ops, hx in disasm(rva, n, count):
        print(f"     {hex(off):>12}: {mn:<7} {ops}")

# RDATA range(s)
rdata_ranges = [(va, end, raw, rawsz) for va, end, raw, name, ex, rawsz in sections if name == ".rdata"]
print("# sections:", ", ".join(f"{n}[{hex(va)}..{hex(e)}]{'X' if ex else ''}" for va,e,_,n,ex,_ in sections))
print()

def find_rdata_qword(target_va):
    """Return list of RVAs in .rdata whose qword == target_va."""
    hits = []
    for va, end, raw, rawsz in rdata_ranges:
        # scan raw bytes for the little-endian qword
        needle = struct.pack("<Q", target_va)
        blob = DATA[raw:raw+rawsz]
        idx = 0
        while True:
            i = blob.find(needle, idx)
            if i < 0: break
            if i % 8 == 0 or True:  # report all 8-aligned-ish; vtable slots are 8-aligned
                hits.append(va + i)
            idx = i + 1
    return hits

# ============================================================
# IGame / CGame vtable — anchor on CGame::Update RVA 0x00667B24
# ============================================================
print("="*70)
print("IGame / CGame vtable  (anchor: CGame::Update RVA 0x00667B24, canonical slot 7)")
print("="*70)
UPDATE_VA = IMAGE_BASE + 0x00667B24
update_hits = find_rdata_qword(UPDATE_VA)
update_hits = [h for h in update_hits if h % 8 == 0]
print(f"CGame::Update VA {hex(UPDATE_VA)} appears in .rdata (8-aligned) at: {[hex(h) for h in update_hits]}")

igame_vtable = None
for h in update_hits:
    # If this is slot 7 of the IGame vtable, the vtable base is h - 7*8.
    base = h - 7*8
    # Validate: base..base+17 should all be in-image code pointers.
    ok = True
    ptrs = []
    for i in range(0, 17):
        v = qword(base + i*8)
        if v is None: ok = False; break
        sn, sx = section_of(v - IMAGE_BASE)
        ptrs.append(v - IMAGE_BASE if v else 0)
        if not (sn and sx): ok = False; break
    print(f"  candidate base {hex(base)} (= {hex(h)} - 0x38): all-17-code-ptrs={ok}")
    if ok:
        igame_vtable = base

if igame_vtable is not None:
    print(f"\n  IGame vtable base = {hex(igame_vtable)}")
    print("  Slot dump (slot: target_rva):")
    for i in range(0, 18):
        v = qword(igame_vtable + i*8)
        tr = v - IMAGE_BASE if v else None
        mark = ""
        if i == 4:  mark = " <- seed CompleteInit (id19)"
        if i == 7:  mark = " <- CGame::Update anchor"
        if i == 12: mark = " <- seed GetLongName (id23)"
        if i == 13: mark = " <- seed GetName (id24)"
        if i == 16: mark = " <- seed GetIGameFramework (id22)"
        print(f"    [{i:>2}] {hex(tr) if tr else '0'}{mark}")
    # Disassemble the asserted slots
    for slot, who in [(4,"CompleteInit"),(12,"GetLongName"),(13,"GetName"),(16,"GetIGameFramework")]:
        tr = qword(igame_vtable + slot*8) - IMAGE_BASE
        show(tr, f"IGame[{slot}] = {who}?")
else:
    print("  COULD NOT validate an IGame vtable base from Update anchor.")

# ============================================================
# CScriptSystem vtable — known RVA 0x03B8AF70 (seed id 119)
# ============================================================
print()
print("="*70)
print("IScriptSystem / CScriptSystem vtable  (known RVA 0x03B8AF70, seed id 119)")
print("="*70)
CSS_VT = 0x03B8AF70
LUA_CREATETABLE = 0x0071F098
print(f"lua_createtable target = {hex(LUA_CREATETABLE)} (seed id 35)")
print("Slot dump [0..18] (slot: target_rva  [calls lua_createtable?]):")
for i in range(0, 19):
    v = qword(CSS_VT + i*8)
    tr = v - IMAGE_BASE if v else None
    # check if the function body calls lua_createtable
    calls_ct = ""
    if tr:
        for off, mn, ops, hx in disasm(tr, 200, 80):
            if mn in ("call","jmp") and hex(LUA_CREATETABLE) in ops:
                calls_ct = f"  -> calls/jmps lua_createtable @ {hex(off)}"
                break
            # also direct call to the createtable VA absolute
    mark = ""
    if i == 12: mark = " <- canonical CreateTable? (per +0 reading)"
    if i == 13: mark = " <- seed CreateTable (id20)"
    print(f"    [{i:>2}] {hex(tr) if tr else '0'}{calls_ct}{mark}")

print("\nDisasm of slot 12 and slot 13 (distinguish CreateTable(bool) from neighbor):")
for slot in (12, 13):
    tr = qword(CSS_VT + slot*8) - IMAGE_BASE
    show(tr, f"CScriptSystem[{slot}]", n=96, count=30)

# ============================================================
# IScriptTable vtable — locate via CScriptSystem::CreateTable body
# ============================================================
print()
print("="*70)
print("IScriptTable vtable  (locate via CreateTable body's vptr store)")
print("="*70)
# CreateTable returns an IScriptTable*; its implementation writes the concrete
# vtable into the new object's [obj+0] (lea reg,[rip+vtbl]; mov [rax],reg).
# We disassemble both slot-12 and slot-13 bodies (and the lua_createtable-adjacent
# allocator) hunting for a `lea rXX, [rip+disp]` whose target lands in .rdata and
# whose target points at a table of code pointers (a vtable).
def find_lea_rdata_vtables(rva, scan=400):
    b = read(rva, scan)
    found = []
    if not b: return found
    for ins in md.disasm(b, IMAGE_BASE + rva):
        if ins.mnemonic == "lea" and "rip" in ins.op_str:
            # capstone gives effective addr in op_str like "rax, [rip + 0x...]"
            for op in ins.operands:
                if op.type == 3:  # X86_OP_MEM
                    if op.mem.base == 0:  # rip-relative shows base as RIP -> capstone resolves
                        pass
            # parse displacement target from ins
            try:
                disp = ins.disp
                tgt = ins.address + ins.size + disp - IMAGE_BASE
            except Exception:
                continue
            sn, sx = section_of(tgt)
            if sn == ".rdata":
                # is it a vtable? first 3 qwords code pointers?
                codeptrs = 0
                for k in range(0,4):
                    v = qword(tgt + k*8)
                    if v:
                        s2,x2 = section_of(v - IMAGE_BASE)
                        if s2 and x2: codeptrs += 1
                found.append((ins.address-IMAGE_BASE, tgt, codeptrs))
    return found

for slot in (12,13):
    tr = qword(CSS_VT + slot*8) - IMAGE_BASE
    leas = find_lea_rdata_vtables(tr, 500)
    print(f"CScriptSystem[{slot}] @ {hex(tr)} rip-rel .rdata LEAs (lea_site, target, leading_codeptrs):")
    for site, tgt, cp in leas:
        print(f"    lea@{hex(site)} -> .rdata {hex(tgt)}  leading_codeptrs={cp}{'  <- vtable-like' if cp>=3 else ''}")

# CreateTable(bool) (CScriptSystem slot 13 @ 0x71a204) calls the object allocator/ctor
# 0x71ed18, which does `lea rcx,[rip+vtbl]; mov [rax],rcx` to install the concrete
# IScriptTable vtable. That vtable is at RVA 0x3a49c70.
print("\n  CreateTable's allocator/ctor 0x71ed18 installs the IScriptTable vtable:")
for site, tgt, cp in find_lea_rdata_vtables(0x71ed18, 400):
    print(f"    lea@{hex(site)} -> .rdata {hex(tgt)} leading_codeptrs={cp}{'  <- CONCRETE IScriptTable vtable' if cp>=4 else ''}")

CST_VT = 0x3a49c70
print(f"\n  CScriptTable vtable @ {hex(CST_VT)} — slot dump [0..9] (canonical: Pad0,dtor,GetScriptSystem,AddRef,Release,Delegate,GetUserDataValue,SetValueAny,...):")
for i in range(0, 10):
    tr = qword(CST_VT + i*8) - IMAGE_BASE
    mark = " <- seed SetValueAny (id21)" if i == 7 else ""
    print(f"    [{i}] {hex(tr)}{mark}")
print("\n  AddRef[3]/Release[4] fingerprints + SetValueAny[7] ABI:")
show(qword(CST_VT+3*8)-IMAGE_BASE, "CScriptTable[3] AddRef (expect inc dword[rcx+8])", n=12, count=3)
show(qword(CST_VT+4*8)-IMAGE_BASE, "CScriptTable[4] Release (expect dec dword[rcx+8])", n=14, count=3)
show(qword(CST_VT+7*8)-IMAGE_BASE, "CScriptTable[7] SetValueAny (expect rcx,rdx,r8,r9b ABI)", n=120, count=22)
