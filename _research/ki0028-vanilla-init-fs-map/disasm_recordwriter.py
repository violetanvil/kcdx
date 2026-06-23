import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_REG_RIP, X86_OP_MEM, X86_OP_IMM
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
base=pe.OPTIONAL_HEADER.ImageBase
sections=pe.sections
text=next(s for s in sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
def secfor(rva):
    for s in sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData): return s
    return None
def cstr(va):
    rva=va-base; s=secfor(rva)
    if not s: return None
    d=s.get_data(); o=rva-s.VirtualAddress; e=d.find(b"\x00",o)
    if e<0 or e-o>200: return None
    try: return d[o:e].decode("latin1")
    except: return None
def find_start(rva,maxback=0x3000):
    i=rva-tva; lim=max(1,i-maxback)
    while i>lim:
        if tdata[i-1]==0xCC: return tva+i
        i-=1
    return None
def callers_of(target_rva):
    res=[]; tgt_va=base+target_rva
    for ins in md.disasm(tdata, base+tva):
        if ins.mnemonic=="call":
            try: ops=ins.operands
            except: continue
            if ops and ops[0].type==X86_OP_IMM and ops[0].imm==tgt_va:
                res.append((ins.address-base, find_start(ins.address-base)))
    return res
out=[]
# Who calls the LevelInfo singleton-getter 0x178b86c? Those touch level metadata.
# Actually find callers of CResourceList::Load via its presence in vtable 0x3a3bcc0 slot? It IS a ctor-set object.
# The record writer: find SetCurrentLevel = writes [ILevelSystem+0x58]. ILevelSystem reached via Game[0x88].
# Approach: find every 'mov [reg+0x58], reg2' in .text whose function ALSO has a call to 0x4dcb60-region OR references 'levels/'.
# Simpler decisive: callers of the resourcelist loader's PARENT. The reslist obj is built in 0x4dcb60; who calls 0x4dcb60? none direct (vtable).
# Find the vtable(s) containing 0x4dcb60 as a slot.
needle=(0x1804dcb60).to_bytes(8,'little')
hits=[]
for s in sections:
    if s.Name.rstrip(b'\x00') not in (b'.rdata',b'.data'): continue
    d=s.get_data(); idx=0
    while True:
        p=d.find(needle, idx)
        if p<0: break
        hits.append(base+s.VirtualAddress+p); idx=p+8
out.append("=== data refs (vtable slots) holding 0x4dcb60 ===")
for h in hits: out.append(f"   0x{h:012x}")
# callers of 0x178b86c (LevelInfo getter)
out.append("\n=== callers of LevelInfo-getter 0x178b86c ===")
for site,fs in callers_of(0x178b86c):
    out.append(f"   call@0x{site:08x} func 0x{(fs or 0):08x}")
open("_recordwriter.txt","w",encoding="utf-8").write("\n".join(out))
print("\n".join(out))
