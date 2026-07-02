"""KI-0028 bridge proof — read the WHGame body that calls fileno() on FOpen's return.

From the Z2.3-open crash dump: fileno() faulted with rcx=3 (a kcdx handle, not a
FILE*). The fileno CALLER is WHGame+0x460cc5 (frame 01), its caller WHGame+0x460b99
(frame 02). Read 0x460b40..0x460d40 to see:
  - the call to FOpen (or the kcdx-served open) whose return feeds fileno
  - the fileno call itself (import thunk)
  - what the body does with fileno's result (the fd) — is it a read the backdrop
    load depends on?
This proves whether the SAME FOpen-return-value -> fileno path the open-only crash
hit is the path the full-swap black screen also traverses (the bridge).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True); pe.parse_data_directories()
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
iat = {}
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for mod in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in mod.imports:
            if imp.name: iat[imp.address] = imp.name.decode()

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
def read_cstr(rva, maxn=100):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off+maxn]; end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii", "ignore"); return s if s.isprintable() else None
    except Exception: return None
def rip_rva(insn):
    op = insn.op_str
    if "rip + " in op:
        try: d = int(op.split("rip + ")[1].split("]")[0], 16)
        except Exception: return None
    elif "rip - " in op:
        try: d = -int(op.split("rip - ")[1].split("]")[0], 16)
        except Exception: return None
    else: return None
    return (insn.address + insn.size - base) + d

def dump(start, end, label):
    print(f"\n=== {label}: 0x{start:x}..0x{end:x} ===")
    d = pe.get_data(start, end - start)
    for insn in md.disasm(d, base + start):
        rva = insn.address - base; note = ""
        m = insn.mnemonic
        if m.startswith("j") and insn.op_str.startswith("0x"):
            t = int(insn.op_str, 16) - base
            note += f"  [{'BACK' if t < rva else 'fwd'}->0x{t:x}]"
        if m == "call" and insn.op_str.startswith("0x"):
            note += f"  -> fn 0x{int(insn.op_str,16)-base:x}"
        tgt = rip_rva(insn)
        if tgt is not None:
            tv = base + tgt
            if m in ("call", "jmp") and "[rip" in insn.op_str and tv in iat:
                note += f"  ; IMPORT {iat[tv]}"
            else:
                s = read_cstr(tgt); note += f"  ;[{sect(tgt)}]0x{tgt:x}" + (f' "{s}"' if s else "")
        mark = ""
        if rva == 0x460cc5: mark = "   <<< frame 01 (fileno caller ret addr)"
        print(f"0x{rva:08x} {m:<8}{insn.op_str}{note}{mark}")

# The fileno caller body — read generously around the two crash frames.
dump(0x460b40, 0x460d40, "fileno-caller region (frames 01 @0x460cc5 / 02 @0x460b99)")
