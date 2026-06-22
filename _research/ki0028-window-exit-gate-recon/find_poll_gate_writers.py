"""KI-0028 — the focus-poll 0x865fb4 exit gates and their writers.

The poll has these exit predicates (each `jmp 0x86609e` = exit; loop continues only if ALL fail):
  G1 [rbx+0x5c1]==0?      object-flag on the dispatcher 'this' (rbx)
  G2 [rbx+0x5b1]==0?      object-flag
  G3 [0x492ba39]!=0       .data byte
  G4 [0x4927260]==0       .data dword  (je exit -> exits when ZERO)
  G5 call[this+0x2d0]->call[+0x740] -> rsi (expected handle); rsi==0 -> exit
  G6 GetActiveWindow()==rsi -> exit  (THE window-focus gate)
  G7 [0x492b890]->[+0x80]->[+0x2b8] returns true -> exit  (winmgr boolean)
The loop SLEEPS and re-polls only while none fire (bounded edi<5).

For the .data poll-gate globals G3/G4, find WRITERS (mov[rip],reg/imm incl op 0x88/0x89/0xC7,
8/16/32-bit). For each writer report enclosing fn + strings. These are the poll-internal gates
PROBE M did NOT track (PROBE M tracked the 0x869c39 loop globals).
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
def fn_strings(fn_va, span=0x900, limit=8):
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

# writers: scan 0x88/0x89 (mov r/m,reg), 0x66 0x89 (16-bit), 0xC6 (mov r/m8,imm8), 0xC7 (imm32)
def writers(target_va):
    res=[]; i=0; n=len(data)
    while i<n-10:
        b=data[i]; k=i; pre66=False
        if b==0x66: pre66=True; k=i+1; b=data[k]
        if 0x40<=b<=0x4f: k+=1; b=data[k]
        rm_ok=False; ilen=0; kind=None
        if b in (0x88,0x89,0x8a,0x8b):  # 8a/8b are loads(reg<-mem) skip; only stores 88/89
            modrm=data[k+1]
            if (modrm&0xC7)==0x05:
                disp=int.from_bytes(data[k+2:k+6],"little",signed=True)
                site=ts+i; ilen=(k+6)-i; tgt=site+ilen+disp
                if b in (0x88,0x89) and tgt==target_va:
                    res.append((site,f"mov[G],reg op{b:#x}{' 16b' if pre66 else ''}"))
            i=k+6; continue
        if b==0xC6:  # mov r/m8, imm8
            modrm=data[k+1]
            if (modrm&0xC7)==0x05 and (modrm&0x38)==0:
                disp=int.from_bytes(data[k+2:k+6],"little",signed=True)
                site=ts+i; ilen=(k+7)-i; tgt=site+ilen+disp
                if tgt==target_va: res.append((site,f"mov[G],imm8 0x{data[k+6]:x}"))
            i=k+7; continue
        if b==0xC7:
            modrm=data[k+1]
            if (modrm&0xC7)==0x05 and (modrm&0x38)==0:
                disp=int.from_bytes(data[k+2:k+6],"little",signed=True)
                imm=int.from_bytes(data[k+6:k+10],"little")
                site=ts+i; ilen=(k+10)-i; tgt=site+ilen+disp
                if tgt==target_va: res.append((site,f"mov[G],imm32 0x{imm:x}"))
            i=k+10; continue
        i+=1
    return res

for rva,label in [(0x492ba39,"G3 poll-gate byte 0x492ba39"), (0x4927260,"G4 poll-gate dword 0x4927260")]:
    va=base+rva
    ws=writers(va)
    print(f"\n=== WRITERS of {label} (VA {va:#x}): {len(ws)} ===")
    for site,kind in ws:
        fn=encl(site); ss=fn_strings(fn) if fn else []
        print(f"  write @ {site:#x} (RVA {site-base:#x}) [{kind}] in fn {fn:#x} (RVA {fn-base:#x})" if fn else f"  write @ {site:#x} [{kind}]")
        if ss: print(f"      strings: "+" | ".join(f'"{x}"' for x in ss))
