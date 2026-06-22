"""KI-0028 — read the constructor/installer of the wedge object 0x549b4a0 (fn 0xda6504)
and resolve the vtable it installs so we can name slot +0x40 (the wedge call).

fn 0xda6504 does: mov [0x549b4a0],0 ; ... ; mov [0x549b4a0],rax  (rax = the new object).
Read the whole fn: the `new`/alloc, the vtable lea (the object's vtable address), any
strings. Then dump the vtable's slot +0x40 target (the function the wedge invokes) and its
neighbors, and read that function's head for strings + a Sleep/wait/import.
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
def qword(rva):
    off=rva_off(rva)
    if off is None: return None
    return int.from_bytes(pe.__data__[off:off+8],"little")
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

def dump(start,end,label):
    print(f"\n=== {label}: 0x{start:x}..0x{end:x} ===")
    d=pe.get_data(start,end-start)
    vtable_leas=[]
    for insn in md.disasm(d, base+start):
        rva=insn.address-base; note=""
        if insn.mnemonic=="call" and insn.op_str.startswith("0x"):
            note+=f"  -> fn 0x{int(insn.op_str,16)-base:x}"
        tgt=rip_rva(insn)
        if tgt is not None:
            tv=base+tgt
            if insn.mnemonic in ("call","jmp") and "[rip" in insn.op_str and tv in iat:
                note+=f"  ; IMPORT {iat[tv]}"
            else:
                s=read_cstr(tgt); note+=f"  ;[{sect(tgt)}]0x{tgt:x}"+(f' "{s}"' if s else "")
                if insn.mnemonic=="lea" and sect(tgt) in (".rdata",".data"):
                    vtable_leas.append(tgt)
        print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
        if insn.mnemonic=="ret": break
    return vtable_leas

leas = dump(0xda6564, 0xda6720, "REAL wedge-obj installer (contains writes 0xda657f/0xda65a6)")

# The object's vtable is the lea written to [obj+0] right before/at construction. Try each
# candidate .rdata lea as a vtable: read slot +0x40 (8th qword) and name it.
print("\n=== candidate vtables from ctor's .rdata leas — slot +0x40 (wedge slot) ===")
for vt in leas:
    if sect(vt) != ".rdata": continue
    slot40 = qword(vt + 0x40)
    if slot40 is None or not (base <= slot40 < base+0x6000000): continue
    s40 = slot40 - base
    # name slot40 by first strings
    strs=[]
    try:
        d=pe.get_data(s40, 0x600)
        for ins in md.disasm(d, slot40):
            if ins.mnemonic=="lea" and "rip" in ins.op_str:
                t=rip_rva(ins)
                if t is not None:
                    st=read_cstr(t)
                    if st and st not in strs: strs.append(st)
                    if len(strs)>=5: break
            if ins.mnemonic=="int3": break
    except Exception: pass
    print(f"  vtable@0x{vt:x}: [+0x40] -> fn 0x{s40:x}" + (f"  strings: "+" | ".join(f'\"{x}\"' for x in strs) if strs else ""))
    # also dump slot +0x40 fn head to see if it Sleeps/waits
