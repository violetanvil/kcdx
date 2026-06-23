import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_REG_RIP, X86_OP_MEM
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
def end_of(rva_start):
    i=rva_start-tva
    while i<len(tdata)-1:
        if tdata[i]==0xCC and (i+1>=len(tdata) or True):
            # next int3 boundary; but functions can contain int3 padding only at end
            return tva+i
        i+=1
    return tva+len(tdata)
def dump(rva_start, label, maxn=0x800):
    off=rva_start-tva
    out=[f"===== {label} 0x{rva_start:08x} ====="]
    cnt=0
    for ins in md.disasm(tdata[off:off+maxn], base+rva_start):
        ann=""
        try: ops=ins.operands
        except: ops=[]
        for op in ops:
            if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP and op.mem.index==0:
                tgt=ins.address+ins.size+op.mem.disp
                s=cstr(tgt)
                if s and s.isprintable() and len(s)>2: ann="  ; \""+s[:60]+"\""
                else: ann=ann or f"  ; -> 0x{tgt-base:x}"
        line=f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{ann}"
        out.append(line)
        cnt+=1
        # stop at a ret followed by int3 (function end heuristic)
        if ins.mnemonic=="ret":
            nb=ins.address-base+ins.size-tva
            if nb<len(tdata) and tdata[nb]==0xCC:
                break
    return "\n".join(out)
RESULT=[]
RESULT.append(dump(0x4dcb60, "RESLIST_PATHBUILDER_0x4dcb60", 0x900))
RESULT.append("")
RESULT.append(dump(0x178b86c, "LEVELINFO_XML_0x178b86c", 0xa00))
open("_bodies.txt","w",encoding="utf-8").write("\n".join(RESULT))
print("wrote _bodies.txt; lines:", sum(len(r.split(chr(10))) for r in RESULT))
