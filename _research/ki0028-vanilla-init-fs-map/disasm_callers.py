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
def dump(rva_start, label, maxn=0x600):
    off=rva_start-tva
    out=[f"===== {label} 0x{rva_start:08x} ====="]
    for ins in md.disasm(tdata[off:off+maxn], base+rva_start):
        ann=""
        try: ops=ins.operands
        except: ops=[]
        for op in ops:
            if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP and op.mem.index==0:
                tgt=ins.address+ins.size+op.mem.disp
                s=cstr(tgt)
                if s and s.isprintable() and len(s)>2: ann="  ; \""+s[:55]+"\""
                else: ann=ann or f"  ; ->0x{tgt-base:x}"
        out.append(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{ann}")
        if ins.mnemonic=="ret":
            nb=ins.address-base+ins.size-tva
            if nb<len(tdata) and tdata[nb]==0xCC: break
    return "\n".join(out)

# find direct callers (E8 rel32) of 0x4dcb60
def callers_of(target_rva):
    res=[]
    tgt_va=base+target_rva
    for ins in md.disasm(tdata, base+tva):
        if ins.mnemonic=="call":
            try: ops=ins.operands
            except: continue
            if ops and ops[0].type==X86_OP_IMM and ops[0].imm==tgt_va:
                fs=find_start(ins.address-base)
                res.append((ins.address-base, fs))
    return res

RESULT=[]
RESULT.append("=== direct callers of CResourceList::Load 0x4dcb60 ===")
for site,fs in callers_of(0x4dcb60):
    RESULT.append(f"   call@0x{site:08x} in func 0x{(fs or 0):08x}")
# the path-builder 0x4dd384 and the FRead-ish helpers
RESULT.append("")
RESULT.append(dump(0x4dd384, "PATHBUILDER_0x4dd384", 0x300))
open("_callers.txt","w",encoding="utf-8").write("\n".join(RESULT))
print("\n".join(RESULT))
