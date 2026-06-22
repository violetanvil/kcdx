"""KI-0028 — identify the wedge object pointer 0x549b4a0 (the call[[0x549b4a0]+0x40] target).

Find every rip-relative reference to 0x549b4a0 (read or write, any opcode) via precise
per-site capstone decode (no linear drift). Classify writes (qword store) vs reads. The
WRITER is the constructor/installer — read its enclosing fn + strings to ID the subsystem.
This object is what the 0x869c39 call_once routine dispatches into at the wedge; identifying
it names the actual actor that hangs (the prior recon left 0x549b4a0 UNIDENTIFIED).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_OP_REG

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); tva = text.VirtualAddress; ts = base + tva
TARGET = base + 0x549b4a0

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
    off=va-ts; j=off; lim=max(0,off-0x8000)
    while j>lim:
        if data[j]==0xCC and data[j+1]!=0xCC: return ts+j+1
        j-=1
    return None
def fn_strings(fn_va, span=0xc00, limit=8):
    out=[]
    try: d=pe.get_data(fn_va-base, span)
    except Exception: return out
    for ins in md.disasm(d, fn_va):
        if ins.mnemonic=="lea" and "rip" in ins.op_str:
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

refs=[]
i=0; n=len(data); seen=set()
while i<n-7:
    p=i
    if 0x40<=data[p]<=0x4f: p+=1
    op=data[p]; modrm=data[p+1] if p+1<n else 0
    if op in (0x88,0x89,0x8a,0x8b,0x8d,0x03,0x2b,0x3b,0x39,0x01,0xc7,0xff,0x85,0x84,0x63) and (modrm&0xC7)==0x05:
        for ins in md.disasm(data[i:i+15], ts+i):
            for o in ins.operands:
                if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:
                    t=ins.address+ins.size+o.mem.disp
                    if t==TARGET:
                        # write if dest operand is the mem and a store opcode
                        is_store = ins.mnemonic in ("mov","movzx") and ins.op_str.strip().startswith("qword ptr [rip")
                        refs.append((ins.address,ins.mnemonic,ins.op_str,is_store))
            i+=ins.size; break
        else: i+=1
        continue
    i+=1

print(f"=== refs to wedge obj 0x549b4a0: {len(refs)} ===")
writes=[r for r in refs if r[3]]
reads=[r for r in refs if not r[3]]
print(f"  writes(qword store): {len(writes)}   reads: {len(reads)}\n")
for addr,mn,ops,isw in writes:
    fn=encl(addr); ss=fn_strings(fn) if fn else []
    print(f"  WRITE @ {addr:#x} (RVA {addr-base:#x}) {mn} {ops}")
    print(f"        in fn {fn:#x} (RVA {fn-base:#x})" + (f"  strings: "+" | ".join(f'\"{x}\"' for x in ss) if ss else ""))
print("\n  -- read sites (enclosing fns) --")
seenfn=set()
for addr,mn,ops,isw in reads:
    fn=encl(addr)
    if fn in seenfn: continue
    seenfn.add(fn)
    ss=fn_strings(fn) if fn else []
    print(f"  read fn {fn:#x} (RVA {fn-base:#x})" + (f"  strings: "+" | ".join(f'\"{x}\"' for x in ss[:4]) if ss else ""))
