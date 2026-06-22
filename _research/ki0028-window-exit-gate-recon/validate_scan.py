"""Validate the mov[rip],reg scanner against KNOWN writes in the 0x869c39 loop body:
  0x869b72 mov [0x556d080], bl
  0x869c1d mov [0x556d084], eax
  0x869c85 mov [0x556d084], eax
  0x869cb5 mov [0x556d080], bl
If the scanner finds these, it works and the guard singletons genuinely have no
mov[rip],reg/imm writer (=> written via a taken-pointer/lea+indirect, or via a
different addressing form). Also scan for LEA of each guard (address-taken) to find
where a pointer to it is computed (the real init path).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); tva = text.VirtualAddress; ts = base + tva

FLAGS = {0x18556d080: "flagByte 0x556d080", 0x18556d084: "flagDword 0x556d084"}
GUARDS = {0x18492b4a0:"WEDGE 0x549b4a0",0x18492b8c0:"0x492b8c0",0x18492b8a8:"0x492b8a8",
          0x18492b908:"0x492b908",0x18492b890:"0x492b890",0x18492b880:"0x492b880",
          0x18492b8a8:"0x492b8a8"}

def scan_mov_store(targets):
    hits = {va: [] for va in targets}
    i=0; n=len(data)
    while i < n-7:
        b=data[i]; k=i
        if 0x40<=b<=0x4f: k=i+1; b=data[k]
        if b in (0x88,0x89):  # 88=mov r/m8,r8 ; 89=mov r/m,reg
            modrm=data[k+1]
            if (modrm&0xC7)==0x05:
                disp=int.from_bytes(data[k+2:k+6],"little",signed=True)
                site=ts+i; ilen=(k+6)-i; tgt=site+ilen+disp
                if tgt in hits: hits[tgt].append((site,f"mov[G],reg op{b:#x}"))
                i=k+6; continue
        if b==0xC7:
            modrm=data[k+1]
            if (modrm&0xC7)==0x05 and (modrm&0x38)==0:
                disp=int.from_bytes(data[k+2:k+6],"little",signed=True)
                site=ts+i; ilen=(k+10)-i; tgt=site+ilen+disp
                if tgt in hits: hits[tgt].append((site,"mov[G],imm"))
                i=k+10; continue
        i+=1
    return hits

def scan_lea(targets):
    # REX.W 48 8D modrm(mod=00 rm=101) disp32  -> lea reg,[rip+disp]
    hits={va:[] for va in targets}
    i=0;n=len(data)
    while i<n-7:
        if data[i] in (0x48,0x4c) and data[i+1]==0x8d and (data[i+2]&0xC7)==0x05:
            disp=int.from_bytes(data[i+3:i+7],"little",signed=True)
            site=ts+i; tgt=site+7+disp
            if tgt in hits: hits[tgt].append(site)
            i+=7; continue
        i+=1
    return hits

print("=== validate: known flag writes ===")
for va,sites in scan_mov_store(FLAGS).items():
    print(f"{FLAGS[va]}: {len(sites)} writes -> "+", ".join(f'{s:#x}(RVA {s-base:#x},{kind})' for s,kind in sites))

print("\n=== guard singletons: mov-store writers (expect 0 if written via lea+indirect) ===")
for va,sites in scan_mov_store(GUARDS).items():
    print(f"{GUARDS[va]}: {len(sites)} mov-store")

print("\n=== guard singletons: LEA (address-taken) sites — the real init path ===")
def encl(va):
    off=va-ts; j=off; lim=max(0,off-0x6000)
    while j>lim:
        if data[j]==0xCC and data[j+1]!=0xCC: return ts+j+1
        j-=1
    return None
for va,sites in scan_lea(GUARDS).items():
    print(f"{GUARDS[va]}: {len(sites)} lea sites")
    for s in sites[:12]:
        fn=encl(s); print(f"    lea @ {s:#x} (RVA {s-base:#x})  in fn {fn:#x} (RVA {fn-base:#x})" if fn else f"    lea @ {s:#x}")
