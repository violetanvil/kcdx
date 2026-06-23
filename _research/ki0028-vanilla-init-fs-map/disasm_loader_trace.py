import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
base=pe.OPTIONAL_HEADER.ImageBase
sections=pe.sections
text=next(s for s in sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True

def secfor(rva):
    for s in sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s
    return None
def cstr(va):
    rva=va-base; s=secfor(rva)
    if not s: return None
    d=s.get_data(); o=rva-s.VirtualAddress; e=d.find(b"\x00",o)
    if e<0 or e-o>200: return None
    try: return d[o:e].decode("latin1")
    except: return None
def find_start(rva,maxback=0x2000):
    i=rva-tva
    lim=max(1,i-maxback)
    while i>lim:
        if tdata[i-1]==0xCC: return tva+i
        i-=1
    return tva+(rva-tva-maxback)
def dump(rva_start, n, label):
    off=rva_start-tva
    out=[f"===== {label} 0x{rva_start:08x} ====="]
    for ins in md.disasm(tdata[off:off+n], base+rva_start):
        ann=""
        try: ops=ins.operands
        except: ops=[]
        for op in ops:
            if op.type==3 and op.mem.base in (0,16,) and op.mem.index==0:  # rip-rel
                tgt=ins.address+ins.size+op.mem.disp
                s=cstr(tgt)
                if s and s.isprintable() and len(s)>3: ann="  ; \""+s[:60]+"\""
        out.append(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{ann}")
    return "\n".join(out)

RESULT=[]
# (1) confirm the getter body 0x66bbf0
RESULT.append(dump(0x66bbf0, 0x60, "GETTER_0x66bbf0"))

# (2) find every string in the image containing 'resourcelist' (case-insensitive)
RESULT.append("\n===== STRINGS containing 'resourcelist' / 'levelinfo' / 'level.cfg' =====")
needles=[b"resourcelist", b"resourceList", b"ResourceList",
         b"levelinfo", b"LevelInfo", b"level.cfg", b"auto_resourcelist",
         b"levels/", b"Levels/", b"mission_mission0", b"GetLevelInfo", b"SetCurrentLevel"]
hits=[]
for s in sections:
    nm=s.Name.rstrip(b"\x00")
    if nm not in (b".rdata", b".data", b".text"): continue
    d=s.get_data(); svrva=s.VirtualAddress
    for nd in needles:
        idx=0
        while True:
            p=d.find(nd, idx)
            if p<0: break
            # extract the c-string around it
            st=p
            while st>0 and 0x20<=d[st-1]<0x7f: st-=1
            en=d.find(b"\x00", p)
            if en<0: en=p+60
            try: txt=d[st:en].decode("latin1")
            except: txt="<bin>"
            va=base+svrva+st
            hits.append((va, nm.decode(), txt[:80]))
            idx=p+len(nd)
seen=set()
for va,nm,txt in sorted(hits):
    if va in seen: continue
    seen.add(va)
    RESULT.append(f"  0x{va:012x} [{nm}] {txt!r}")

open(r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\_research\ki0028-vanilla-init-fs-map\_loader_trace.txt","w",encoding="utf-8").write("\n".join(RESULT))
print("\n".join(RESULT[:2]))
print("...total string hits:", len(seen))
