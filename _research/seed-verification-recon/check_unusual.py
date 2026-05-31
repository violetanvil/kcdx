#!/usr/bin/env python3
"""Disassemble the first ~6 instructions at each 'UNUSUAL'-prologue row to confirm
the RVA is a real instruction boundary (function entry), not mid-instruction.
A clean decode whose first insn is a plausible entry (test/mov/sub/cmp/push/jmp/ret-stub)
confirms ENTRY; a decode that only makes sense starting a few bytes earlier flags MISMATCH."""
import os, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

REPO = r"c:/Users/Michael/Documents/KCD2 Mods/kcdx"
DLL  = os.path.join(REPO, "third-party-ghidra", "WHGame.dll")
IMAGE_BASE = 0x180000000
pe = pefile.PE(DLL, fast_load=True)
secs = []
for s in pe.sections:
    secs.append((s.VirtualAddress, s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData),
                 s.PointerToRawData, s.SizeOfRawData))
with open(DLL,"rb") as f: DATA=f.read()
def off(rva):
    for va,end,raw,rawsz in secs:
        if va<=rva<end and (rva-va)<rawsz: return raw+(rva-va)
    return None
md = Cs(CS_ARCH_X86, CS_MODE_64)

# (id, rva, name, seed-prose hint)
ROWS = [
 (32,0x0071E77C,"lua_settop","cold-path? prose: settop"),
 (47,0x00B9CE78,"lua_typename","cmp tp,-1 fast-path"),
 (58,0x00A2DA68,"lua_sethook","test rdx"),
 (59,0x00CAE9A8,"lua_getstack","mov r9,[rcx+0x28]"),
 (72,0x00B9D8E4,"luaL_addlstring","test r8 / strlen prep"),
 (82,0x039932DC,"lua_xmove","mov rbx,rdx (xmove 3x site)"),
 (105,0x0071DD7C,"index2adr","test edx; js — lapi helper"),
 (108,0x039934B4,"luaL_addstring","or r8,-1 inline strlen then jmp addlstring"),
 (112,0x0399614C,"luaG_runerror","mov rbx,rsp variadic prologue"),
 (121,0x01448F38,"CScriptSystem_Init","mov [rsp+x],al"),
 (127,0x03997070,"luaC_barrierf","mov rcx,[rcx+0x20]"),
 (135,0x004D9058,"ModManager_Mount","mov rbx,rsp; ... 48 prologue"),
 (104,0x003B70F0,"luaopen_io","STUB: ret 0 thunk per prose"),
]
for rid,rva,nm,hint in ROWS:
    o=off(rva)
    code=DATA[o:o+24]
    print(f"\n=== id{rid} {nm} @ {hex(rva)}  (seed hint: {hint})")
    n=0
    for ins in md.disasm(code, IMAGE_BASE+rva):
        print(f"  {ins.address-IMAGE_BASE:#08x}  {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}")
        n+=1
        if n>=6: break
    # also decode 8 bytes BEFORE to see if rva lands mid-instruction of a prior insn
    pre=DATA[o-8:o+8]
    print(f"  [pre-context decode from rva-8]:")
    for ins in md.disasm(pre, IMAGE_BASE+rva-8):
        marker = "  <-- RVA" if ins.address==IMAGE_BASE+rva else ""
        print(f"    {ins.address-IMAGE_BASE:#08x}  {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}{marker}")
