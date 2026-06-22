"""KI-0028 — read the ACTUAL spin site: the bounded focus poll at 0x866090.

PROBE M / -pv always sampled Main at 0x866090 = `inc edi` right after `call Sleep`
(prior recon: poll body RVA 0x865fb4). That is the real spin, reached THROUGH the wedge
call [[0x549b4a0]+0x40] in the 0x869c39 call_once routine. Read the whole enclosing
function (back-scan to entry) so we can see:
  - the loop's exit branch and the EXACT field/predicate it tests (a window handle compare?
    a GetActiveWindow == expected?),
  - the Sleep call (confirms the spin),
  - the back-edge.
Resolve every rip-global, every import (GetActiveWindow/GetForegroundWindow/Sleep), and
every direct call. AP19: cite the exact test site.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True); pe.parse_data_directories()
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); tva = text.VirtualAddress; ts = base + tva

iat={}
if hasattr(pe,"DIRECTORY_ENTRY_IMPORT"):
    for mod in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in mod.imports:
            if imp.name: iat[imp.address]=imp.name.decode()

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None
def sect(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode()
    return "?"
def read_cstr(rva,maxn=100):
    off=rva_off(rva)
    if off is None: return None
    raw=pe.__data__[off:off+maxn]; end=raw.find(b"\x00")
    if end<=2: return None
    try:
        s=raw[:end].decode("ascii","ignore"); return s if s.isprintable() else None
    except Exception: return None
def rip_rva(insn):
    op=insn.op_str
    if "rip + " in op:
        try: d=int(op.split("rip + ")[1].split("]")[0],16)
        except Exception: return None
    elif "rip - " in op:
        try: d=-int(op.split("rip - ")[1].split("]")[0],16)
        except Exception: return None
    else: return None
    return (insn.address+insn.size-base)+d

def entry_of(rva):
    off=rva-(tva); j=off; lim=max(0,off-0x600)
    while j>lim:
        if data[j]==0xCC and data[j+1]!=0xCC: return tva+j+1
        j-=1
    return rva

# 0x866090 is mid-function; find entry, dump entry..0x866200
entry=entry_of(0x866090)
END=0x866150
print(f"=== focus-poll fn entry 0x{entry:x} .. 0x{END:x} (0x866090 = spin sample) ===\n")
d=pe.get_data(entry, END-entry)
for insn in md.disasm(d, base+entry):
    rva=insn.address-base; note=""
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        t=int(insn.op_str,16)-base
        note+=f"  [{'BACK' if t<rva else 'fwd'}->0x{t:x}]"
    if insn.mnemonic=="call" and insn.op_str.startswith("0x"):
        note+=f"  -> fn 0x{int(insn.op_str,16)-base:x}"
    tgt=rip_rva(insn)
    if tgt is not None:
        tva_=base+tgt
        if insn.mnemonic in ("call","jmp") and "[rip" in insn.op_str and tva_ in iat:
            note+=f"  ; IMPORT {iat[tva_]}"
        else:
            s=read_cstr(tgt); note+=f"  ;[{sect(tgt)}]0x{tgt:x}"+(f' "{s}"' if s else "")
    if rva==0x866090: note+="   <<< SPIN SAMPLE (inc edi after Sleep)"
    print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
