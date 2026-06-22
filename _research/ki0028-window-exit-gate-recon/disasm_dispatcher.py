"""KI-0028 — read the focus-poll's caller 0x667b24 around the call site 0x667ddd, and
resolve the early-exit globals of the poll.

The poll 0x865fb4 is called from 0x667ddd inside fn 0x667b24 ("BreakListenerThread"/
"g_BreakListener" strings -> this is the engine system-tick / message-pump dispatcher).
Read 0x667d80..0x667e60 to see whether the call sits in a loop (back-edge) and what the
surrounding tick does. Also resolve:
  - 0x4927260 (dword tested je at 0x865ff4 -> poll early-exits if ==0)
  - 0x492ba39 (byte tested at 0x865fe7)
These two .data globals are the poll's own gates; if a poll gate global differs swap-on/off
it would be the differentiator PROBE M did NOT track (PROBE M tracked the 0x869c39 loop's
globals, NOT these poll-internal ones).
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

def dump(start,end,label):
    print(f"\n=== {label}: 0x{start:x}..0x{end:x} ===")
    d=pe.get_data(start,end-start)
    for insn in md.disasm(d, base+start):
        rva=insn.address-base; note=""
        if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
            t=int(insn.op_str,16)-base; note+=f"  [{'BACK' if t<rva else 'fwd'}->0x{t:x}]"
        if insn.mnemonic=="call" and insn.op_str.startswith("0x"):
            note+=f"  -> fn 0x{int(insn.op_str,16)-base:x}"
        tgt=rip_rva(insn)
        if tgt is not None:
            tv=base+tgt
            if insn.mnemonic in ("call","jmp") and "[rip" in insn.op_str and tv in iat:
                note+=f"  ; IMPORT {iat[tv]}"
            else:
                s=read_cstr(tgt); note+=f"  ;[{sect(tgt)}]0x{tgt:x}"+(f' "{s}"' if s else "")
        if rva==0x667ddd: note+="   <<< call focus-poll 0x865fb4"
        print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")

dump(0x667b24, 0x667e80, "dispatcher fn 0x667b24 .. past focus-poll call")
