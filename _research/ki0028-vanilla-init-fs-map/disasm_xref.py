import pefile, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_REG_RIP, X86_OP_MEM
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
base=pe.OPTIONAL_HEADER.ImageBase
sections=pe.sections
text=next(s for s in sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True

def find_start(rva,maxback=0x2500):
    i=rva-tva; lim=max(1,i-maxback)
    while i>lim:
        if tdata[i-1]==0xCC: return tva+i
        i-=1
    return tva+(rva-tva-maxback)

# targets to xref (VAs)
targets={
 0x183a3bd00:"auto_resourcelist.txt",
 0x183a3bd18:"resourcelist.txt",
 0x183a3bd40:"Levels/",
 0x184052310:"/LevelInfo.xml",
 0x184052f20:"levels/",
 0x1846ac5c8:"auto_resourcelist_total.txt",
}
# scan .text for rip-relative lea/mov that resolve to a target VA
xrefs={k:[] for k in targets}
for ins in md.disasm(tdata, base+tva):
    try: ops=ins.operands
    except: continue
    for op in ops:
        if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP and op.mem.index==0:
            tgt=ins.address+ins.size+op.mem.disp
            if tgt in targets:
                xrefs[tgt].append(ins.address-base)

out=[]
for va,name in targets.items():
    out.append(f"=== xrefs to {name} (0x{va:x}) ===")
    for x in xrefs[va]:
        fs=find_start(x)
        out.append(f"   site 0x{x:08x}  in func_start 0x{fs:08x}")
open("_xref.txt","w",encoding="utf-8").write("\n".join(out))
print("\n".join(out))
