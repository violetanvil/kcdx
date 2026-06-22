"""KI-0028 — find the init path for the singleton cluster and the wedge object 0x549b4a0.

The five guard singletons (0x492b880/890/8a8/8c0/908) and the wedge object pointer
0x549b4a0 have NO direct mov[rip],reg writer and NO lea[rip] in .text — so they are
written via a register base computed elsewhere (a relocated pointer table or a struct base).

Two scans:
1. ANY lea/mov-load referencing the byte range [0x492b800 .. 0x492b960) -> find the
   cluster base pointer + the function(s) that touch the cluster as a struct.
2. ANY rip-relative reference (read OR write, any width) to 0x549b4a0 -> who installs the
   wedge object pointer, and who else reads it. Then read the vtable of the object it points
   to (need a live pointer; statically we can at least find the writer that stores into it).

Report each ref site + its enclosing fn + that fn's first strings (KI-0026 id).
Also: dump the call[rax+0x40] context — what slot index 0x40 is and the object's other slots.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

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
def read_cstr(rva, maxn=100):
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
def fn_strings(fn_va, span=0x800, limit=6):
    out=[]
    try: d=pe.get_data(fn_va-base, span)
    except Exception: return out
    for ins in md.disasm(d, fn_va):
        for op in ins.operands:
            if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
                t=ins.address+ins.size+op.mem.disp-base
                s=read_cstr(t)
                if s and s not in out:
                    out.append(s)
                    if len(out)>=limit: return out
        if ins.mnemonic=="int3": break
    return out

CLUSTER_LO=0x180000000+0x492b800
CLUSTER_HI=0x180000000+0x492b960
WEDGE=0x180000000+0x492b4a0

cluster_refs=[]; wedge_refs=[]
# full disasm in chunks won't drift if we seed per-instruction from a linear sweep; but
# WHGame linear disasm drifts. Instead: rip-relative refs all have form (REX?) opcode modrm(rm=101)
# disp32. We byte-scan for ANY modrm with mod=00 rm=101 after a 1-2 byte opcode and decode the
# disp; then we re-disasm a SMALL window at that site to get the real mnemonic (alignment-correct).
i=0; n=len(data)
seen=set()
while i<n-7:
    # candidate: optional REX (40-4f), 1-byte opcode, modrm with mod=00 & rm=101
    p=i
    if 0x40<=data[p]<=0x4f: p+=1
    op=data[p]; modrm=data[p+1] if p+1<n else 0
    # cover common opcodes that take modrm: 88,89,8a,8b(mov),8d(lea),03,2b,3b,39,01,c7,ff,f7,85,84
    if op in (0x88,0x89,0x8a,0x8b,0x8d,0x03,0x2b,0x3b,0x39,0x01,0xc7,0xff,0x85,0x84,0x63,0x0f) and (modrm&0xC7)==0x05:
        # decode this single insn precisely with capstone seeded here
        win=data[i:i+15]
        for ins in md.disasm(win, ts+i):
            tgt=None
            for o in ins.operands:
                if o.type==X86_OP_MEM and o.mem.base==X86_REG_RIP:
                    tgt=ins.address+ins.size+o.mem.disp
            if tgt is not None:
                if CLUSTER_LO<=tgt<CLUSTER_HI:
                    cluster_refs.append((ins.address,ins.mnemonic,ins.op_str,tgt))
                if tgt==WEDGE:
                    wedge_refs.append((ins.address,ins.mnemonic,ins.op_str))
            i+=ins.size
            break
        else:
            i+=1
        continue
    i+=1

print(f"=== refs into singleton cluster [0x492b800..0x492b960): {len(cluster_refs)} ===")
# group by enclosing fn
byfn={}
for addr,mn,ops,tgt in cluster_refs:
    fn=encl(addr); byfn.setdefault(fn,[]).append((addr,mn,ops,tgt))
for fn in sorted(k for k in byfn if k):
    ss=fn_strings(fn)
    print(f"\n  fn {fn:#x} (RVA {fn-base:#x})  refs={len(byfn[fn])}" + (f"  strings: "+" | ".join(f'\"{x}\"' for x in ss) if ss else "  (no strings)"))
    for addr,mn,ops,tgt in byfn[fn][:14]:
        w = "WRITE" if mn in ("mov","movzx") and ops.split(",")[0].strip().startswith("qword ptr [rip") else ""
        print(f"      {addr:#x} (RVA {addr-base:#x}) {mn} {ops}  -> 0x{tgt-base:x} {w}")

print(f"\n=== refs to WEDGE obj ptr 0x492b4a0: {len(wedge_refs)} ===")
wbyfn={}
for addr,mn,ops in wedge_refs:
    fn=encl(addr); wbyfn.setdefault(fn,[]).append((addr,mn,ops))
for fn in sorted(k for k in wbyfn if k):
    ss=fn_strings(fn)
    iswrite=any(o[2].split(",")[0].strip().startswith("qword ptr [rip") for o in wbyfn[fn])
    print(f"  fn {fn:#x} (RVA {fn-base:#x})  refs={len(wbyfn[fn])}{'  <<HAS-WRITE' if iswrite else ''}" + (f"  strings: "+" | ".join(f'\"{x}\"' for x in ss) if ss else ""))
    for addr,mn,ops in wbyfn[fn]:
        print(f"      {addr:#x} (RVA {addr-base:#x}) {mn} {ops}")
