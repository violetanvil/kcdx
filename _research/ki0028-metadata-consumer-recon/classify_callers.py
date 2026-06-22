"""Classify each provenance-verified metadata-slot consumer: dump the resolved string
literals it references (LEA rip-rel into .rdata) + the .data globals it touches, so we
can tell graphics/window/device/config/shader init from gameplay/audio/etc.

For each func start, disassemble up to 0x600 bytes; collect:
  - LEA reg,[rip+d] targets landing in .rdata -> read the C-string there
  - the metadata-slot call(s) + the branch right after (je/jne) to show if/else on existence
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
            if end < 0 or end - off > maxlen: end = min(off+maxlen, len(d))
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

# the 44 funcs (dedup), tagged by slot
FUNCS = {
 0x244dd9c:"s45", 0x39b63a8:"s45", 0x39b6bfc:"s45",
 0x4d8e14:"s67",0x51c248:"s67",0x51e77b:"s67",0x52d71c:"s67",0x7a06ac:"s67",0x7aa795:"s67",
 0x7b68ac:"s67",0x8118b0:"s67",0x89682d:"s67",0x91f26d:"s67",0x922b10:"s67",0x9807c2:"s67",
 0x9b6828:"s67",0x9b6964:"s67",0x9b6a87:"s67",0x9b6d24:"s67",0xabe964:"s67",0xad549c:"s67",
 0xaeae38:"s67",0xaeb4d2:"s67",0xc18971:"s67",0xded598:"s67",0xe3f8d8:"s67",0xf7efcc:"s67",
 0xfa9ca1:"s67",0x1033d5d:"s67",0x14d5580:"s67",0x182ddb4:"s67",0x1e2adb0:"s67",0x1e960d0:"s67",
 0x1fec920:"s67",0x20f2ae7:"s67",0x21fbc84:"s67",0x21fe704:"s67",0x243d8a4:"s67",0x374d548:"s67",
 0x39a82dc:"s67",
}
GRAPHICS_HINTS = ("shader","Shader","r_","gfx","Render","render","d3d","D3D","DXGI","swap","Swap",
 "device","Device","display","Display","Fullscreen","fullscreen","window","Window","config","Config",
 ".cfx","engine_core","thread_config",".cfg","texture","Texture",".dds",".cgf","pak","Pak",
 "graphics","Graphics","present","Present","NGX","fsr","FSR","upscal","video","Video",".usm",".bk2",
 "resolution","Resolution","cache","Cache","platform","Platform","System","CVar","cvar")

for func_rva in sorted(FUNCS):
    slot = FUNCS[func_rva]
    fva = base + func_rva
    off = fva - ts
    body = tdata[off:off+0x700]
    strs = []
    for ins in md.disasm(body, fva):
        if ins.mnemonic in ("lea","mov") and len(ins.operands)==2:
            d,s = ins.operands
            if s.type==X86_OP_MEM and s.mem.base==X86_REG_RIP:
                tgt = ins.address+ins.size+s.mem.disp
                cs = read_cstr(tgt)
                if cs: strs.append(cs)
        if ins.mnemonic in ("ret","int3") and ins.address > fva+0x40:
            # stop at first ret past a min size (rough func end)
            pass
    hints = [x for x in strs if any(h in x for h in GRAPHICS_HINTS)]
    flag = "  <<< GFX/INIT" if hints else ""
    show = hints[:6] if hints else strs[:4]
    print(f"[{slot}] func RVA {func_rva:#x}{flag}")
    for x in show:
        print(f"      \"{x}\"")
    if not show:
        print("      (no resolved strings in first 0x700)")
