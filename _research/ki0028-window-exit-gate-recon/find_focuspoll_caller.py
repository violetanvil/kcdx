"""KI-0028 — find the OUTER loop that re-calls the bounded focus-poll 0x865fb4.

The 0x865fb4 poll is bounded (edi<5). Main samples perpetually at 0x866090, so an OUTER
loop re-invokes 0x865fb4 every iteration. Find the direct callers of 0x865fb4 (byte-scan
.text for E8 rel32 whose target == 0x865fb4), read each caller's body around the call to
see the enclosing loop and the field its exit branch tests.

Also: the chain prior recon claimed is 0x869c39 -> [[0x549b4a0]+0x40] -> ... -> 0x86b017 ->
0x865fb4. We can't resolve the vtable indirect statically, but we CAN find every DIRECT
caller of 0x865fb4 and of 0x86b017 and see which encloses a loop.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); tva = text.VirtualAddress; ts = base + tva

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None
def read_cstr(rva,maxn=100):
    off=rva_off(rva)
    if off is None: return None
    raw=pe.__data__[off:off+maxn]; end=raw.find(b"\x00")
    if end<=2: return None
    try:
        s=raw[:end].decode("ascii","ignore"); return s if s.isprintable() else None
    except Exception: return None
def encl(va):
    off=va-ts; j=off; lim=max(0,off-0x6000)
    while j>lim:
        if data[j]==0xCC and data[j+1]!=0xCC: return ts+j+1
        j-=1
    return None
def fn_strings(fn_va, span=0x900, limit=6):
    out=[]
    try: d=pe.get_data(fn_va-base, span)
    except Exception: return out
    for ins in md.disasm(d, fn_va):
        if "lea" == ins.mnemonic and "rip" in ins.op_str:
            op=ins.op_str
            try:
                if "rip + " in op: disp=int(op.split("rip + ")[1].split("]")[0],16)
                elif "rip - " in op: disp=-int(op.split("rip - ")[1].split("]")[0],16)
                else: continue
            except Exception: continue
            t=ins.address+ins.size+disp-base; s=read_cstr(t)
            if s and s not in out:
                out.append(s)
                if len(out)>=limit: break
        if ins.mnemonic=="int3": break
    return out

def callers_of(target_rva):
    tgt=base+target_rva; res=[]
    i=0; n=len(data)
    while i<n-5:
        if data[i]==0xE8:
            rel=int.from_bytes(data[i+1:i+5],"little",signed=True)
            site=ts+i; dest=site+5+rel
            if dest==tgt: res.append(site)
            i+=5; continue
        i+=1
    return res

for target,label in [(0x865fb4,"focus-poll 0x865fb4"), (0x86b017,"0x86b017 (frame above poll)")]:
    cs=callers_of(target)
    print(f"\n=== DIRECT callers of {label}: {len(cs)} ===")
    for site in cs:
        fn=encl(site); ss=fn_strings(fn) if fn else []
        print(f"  call @ {site:#x} (RVA {site-base:#x})  in fn {fn:#x} (RVA {fn-base:#x})" if fn else f"  call @ {site:#x}")
        if ss: print(f"      strings: "+" | ".join(f'"{x}"' for x in ss))
