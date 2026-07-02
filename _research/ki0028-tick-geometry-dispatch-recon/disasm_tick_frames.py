"""KI-0028 Measurement 2 — disassemble the tick DISPATCHER frames PROBE Y captured on Main's
stack, to find where the per-frame tick CONDITIONALLY dispatches scene-geometry/level-advance
work and what state gates it.

PROBE Y (stall_no_geometry dump) captured Main's stack (swap-ON, present=4513, draw_indexed=0)
with these frames just above HookedUpdate (0x42A1A):
    0x532FB5  ->  0x6678A0  ->  0x667DE2  ->  (HookedUpdate 0x42A1A region)
0x667b24 is the tick dispatcher (BreakListenerThread/g_BreakListener); 0x667DE2 is inside it
(the focus-poll call is at 0x667ddd, one insn earlier). So the tick body 0x667b24.. is where the
per-frame work is dispatched. The geometry-dispatch DECISION — a conditional branch the tick
takes swap-OFF (draw geometry) and does NOT take swap-ON (draw_indexed stays 0) — should live
here or in a callee reached from here.

Method: dump each captured frame's function body, flag every conditional branch + call + the
.data/.bss global each condition reads. A gate whose global is written by level-load / scene /
render-pipeline bring-up is the Measurement-2 candidate to probe swap-on vs swap-off.

Reuse: extends _research/ki0028-window-exit-gate-recon/disasm_dispatcher.py (same helpers), which
targeted only 0x667b24..0x667e80 around the focus-poll call and truncated. This covers the full
frame chain.
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

def fn_end(start, cap=0x1200):
    """Heuristic function end: first ret/int3-pad after start, bounded by cap."""
    d = pe.get_data(start, cap)
    last = start
    for insn in md.disasm(d, base + start):
        last = insn.address - base + insn.size
        if insn.mnemonic == "ret":
            return last
        if insn.mnemonic == "int3" and (insn.address - base) > start + 8:
            return insn.address - base
    return last

def dump(start, label, end=None):
    if end is None: end = fn_end(start)
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
        print(f"0x{rva:08x} {m:<8}{insn.op_str}{note}")

# The PROBE Y frame chain, bottom (deepest) to top:
dump(0x667b24, "TICK DISPATCHER fn 0x667b24 (contains focus-poll call @0x667ddd, frame 0x667DE2)")
dump(0x6678a0, "frame 0x6678A0 caller (above 0x667DE2)")
dump(0x532fb5 - 0x400, "frame 0x532FB5 region (dump from 0x532bb5 to capture the enclosing fn)", end=0x533200)
