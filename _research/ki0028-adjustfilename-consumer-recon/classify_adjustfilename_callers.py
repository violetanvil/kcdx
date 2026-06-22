"""KI-0028 DIVERGENCE-C — classify each slot-1 (AdjustFileName) consumer.

REUSED from ../ki0028-metadata-consumer-recon/classify_callers.py. For each enclosing func,
disassemble the body and collect resolved string literals (LEA/MOV rip-rel into a data section).
Flag any func touching window/swapchain/present/display-mode/message-pump/render vocabulary —
the load-bearing question (does a present/window consumer branch on AdjustFileName's path FORM).

Func list = the 31 distinct enclosing funcs from correlate_adjustfilename_slot.py.
"""
import pefile, string
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL)
base = pe.OPTIONAL_HEADER.ImageBase
secs = [(s.Name.rstrip(b"\x00").decode(errors="replace"),
         base + s.VirtualAddress, base + s.VirtualAddress + len(s.get_data()), s.get_data(), base + s.VirtualAddress)
        for s in pe.sections]

def read_cstr(va, maxlen=120):
    for name, lo, hi, d, secva in secs:
        if lo <= va < hi:
            off = va - secva
            end = d.find(b"\x00", off)
            if end < 0 or end - off > maxlen:
                end = min(off+maxlen, len(d))
            raw = d[off:end]
            try:
                s = raw.decode("ascii")
                if all(c in string.printable for c in s) and len(s) >= 3:
                    return s
            except Exception:
                return None
    return None

text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); ts = base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True

# 31 distinct enclosing funcs (RVA) from the correlation run
FUNCS = [
 0x463a24,0x46a8f0,0x49c7c0,0x4d8c80,0x4f3f10,0x51c0a0,0x6c5860,0x6d0e30,0x6d8a00,
 0x77ef10,0x7a0540,0x7b6700,0x7e4c80,0x811700,0x896700,0x91f100,0x9b6700,0xad5300,
 0xc18800,0xded400,0xe3f700,0xf7ee00,0xfa9b00,0x1033c00,0x14d5400,0x182dc00,0x1e2ac00,
 0x1fec800,0x21fbb00,0x243d700,0x39a8100,
]
# NOTE: actual func RVAs come from the correlation output; this list is a placeholder
# overwritten below by reading _slot1_callers.txt.

import re, os
fpath = os.path.join(os.path.dirname(__file__), "_slot1_callers.txt")
rvas = []
with open(fpath) as fh:
    for line in fh:
        m = re.search(r"func 0x[0-9a-f]+ \(RVA (0x[0-9a-f]+)\)", line)
        if m:
            rvas.append(int(m.group(1), 16))
if rvas:
    FUNCS = sorted(set(rvas))

WIN_HINTS = ("swap","Swap","present","Present","DXGI","d3d","D3D","device","Device",
 "display","Display","Fullscreen","fullscreen","window","Window","HWND","hwnd","WndProc",
 "wndproc","message","Message","PeekMessage","resolution","Resolution","r_Width","r_Height",
 "r_Fullscreen","RenderThread","renderthread","SwapChain","swapchain","Adapter","monitor",
 "Monitor","vsync","VSync","refresh","Refresh","backbuffer","BackBuffer","NGX","fsr","FSR",
 "DLSS","upscal","present_","msg_pump","pump")
GFX_HINTS = ("shader","Shader","r_","gfx","Render","render","graphics","Graphics","texture",
 "Texture","CVar","cvar","engine_core","thread_config","platform","Platform")

for func_rva in FUNCS:
    fva = base + func_rva
    off = fva - ts
    body = tdata[off:off+0x800]
    strs = []
    for ins in md.disasm(body, fva):
        if ins.mnemonic in ("lea","mov") and len(ins.operands) == 2:
            d, s = ins.operands
            if s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
                tgt = ins.address + ins.size + s.mem.disp
                cs = read_cstr(tgt)
                if cs:
                    strs.append(cs)
    win = [x for x in strs if any(h in x for h in WIN_HINTS)]
    gfx = [x for x in strs if any(h in x for h in GFX_HINTS)]
    flag = "  <<< WINDOW/PRESENT/DISPLAY" if win else ("  <<< gfx-vocab" if gfx else "")
    print(f"func RVA {func_rva:#x}{flag}")
    show = (win[:4] or gfx[:4] or strs[:5])
    for x in show:
        print(f'      "{x}"')
    if not show:
        print("      (no resolved strings in first 0x800)")
