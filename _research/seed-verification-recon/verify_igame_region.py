#!/usr/bin/env python3
"""Dump the .rdata region around the CGame::Update slot to find the IGame vtable
bounds and check whether the asserted slots hold the right code pointers.

The earlier all-code-ptr validation FAILED at some slot in [base..base+16]; this
script prints every slot's target + section so we can SEE why (a non-code slot =
pure-virtual filler, a data ptr, or the run not actually being 17 contiguous)."""
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
with open(DLL,"rb") as f: DATA=f.read()
def rva_to_off(rva):
    for va,end,raw,name,ex,rawsz in sections:
        if va<=rva<end:
            d=rva-va
            return raw+d if d<rawsz else None
    return None
def section_of(rva):
    for va,end,raw,name,ex,rawsz in sections:
        if va<=rva<end: return name,ex
    return None,False
def qword(rva):
    o=rva_to_off(rva)
    if o is None: return None
    return struct.unpack("<Q",DATA[o:o+8])[0]
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True

UPDATE_RVA = 0x00667B24
UPDATE_VA  = IMAGE_BASE + UPDATE_RVA
# Slot containing Update found at .rdata 0x3dc18a0 -> if slot 7, base=0x3dc1868
SLOT7_RVA = 0x3dc18a0
base = SLOT7_RVA - 7*8
print(f"Update @ {hex(UPDATE_VA)}; assume that .rdata word {hex(SLOT7_RVA)} is slot 7 -> base {hex(base)}")
print("Full region dump  [slot] target_rva (section/exec):")
# dump a few BEFORE base to find where the vtable actually starts (a vtable is
# preceded by something that is NOT a run of code pointers, or by an RTTI col ptr)
for i in range(-3, 30):
    rva = base + i*8
    v = qword(rva)
    if v is None:
        print(f"   slot[{i:>2}] @ {hex(rva)}: <offmap>"); continue
    tr = v - IMAGE_BASE
    sn,sx = section_of(tr)
    tag = f"{sn}/{'X' if sx else 'data'}" if sn else "NOT-IN-IMAGE"
    note=""
    if v == UPDATE_VA: note=" <<< CGame::Update (canonical slot 7)"
    if i==4: note+=" [id19 CompleteInit]"
    if i==12: note+=" [id23 GetLongName]"
    if i==13: note+=" [id24 GetName]"
    if i==16: note+=" [id22 GetIGameFramework]"
    print(f"   slot[{i:>2}] @ {hex(rva)}: {hex(v)} -> rva {hex(tr)} ({tag}){note}")

# Disasm the slots of interest regardless
def show(rva,label,n=80,count=22):
    b=DATA[rva_to_off(rva):rva_to_off(rva)+n]
    print(f"\n  -- {label} @ RVA {hex(rva)} --")
    c=0
    for ins in md.disasm(b, IMAGE_BASE+rva):
        print(f"     {hex(ins.address-IMAGE_BASE):>10}: {ins.mnemonic:<7} {ins.op_str}")
        c+=1
        if c>=count: break

for slot,who in [(4,"CompleteInit"),(12,"GetLongName"),(13,"GetName"),(16,"GetIGameFramework")]:
    v=qword(base+slot*8)
    if v: show(v-IMAGE_BASE, f"IGame[{slot}] candidate = {who}")
