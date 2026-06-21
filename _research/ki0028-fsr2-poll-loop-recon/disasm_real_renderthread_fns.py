"""KI-0026 trap check: the NGX/FSR2 frame labels are nearest-export NOISE (offsets
2-9MB past the export). Find what the RenderThread is ACTUALLY in. For each real
RVA, disasm a window + scan nearby .rdata string refs (lea rcx,[rip+...]) to
identify the subsystem - exactly KI-0026's identify-by-strings method."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True); md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; IB=0x180000000
def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s.PointerToRawData+(rva-s.VirtualAddress)
def read_cstr(rva,maxn=120):
    off=rva_off(rva)
    if off is None: return None
    raw=pe.__data__[off:off+maxn]
    end=raw.find(b'\x00')
    if end<=0: return None
    try: return raw[:end].decode('ascii')
    except: return None
# The RenderThread wait frame + its 3 callers (real RVAs)
TARGETS={'wait_NGXlabel_0x1de928e':0x1de928e,'caller1_0x9acdfb':0x9acdfb,'caller2_0x8674d3':0x8674d3,'caller3_0xa62b86':0xa62b86}
for name,rva in TARGETS.items():
    print(f'\n=== {name} (RVA 0x{rva:x}) — scan -0x80..+0x40 for string refs that identify the subsystem ===')
    start=rva-0x80
    data=pe.get_data(start,0xC0)
    strs=[]
    for insn in md.disasm(data,IB+start):
        r=insn.address-IB
        # lea reg,[rip+disp] -> a .rdata pointer (likely a string)
        if insn.mnemonic=='lea' and 'rip' in insn.op_str:
            try:
                disp=int(insn.op_str.split('rip +')[1].rstrip(']').strip(),16) if 'rip +' in insn.op_str else -int(insn.op_str.split('rip -')[1].rstrip(']').strip(),16)
                tgt=(insn.address+insn.size-IB)+disp
                s=read_cstr(tgt)
                if s and len(s)>3 and s.isprintable(): strs.append((hex(r),s))
            except: pass
    for r,s in strs[:8]: print(f'   {r}: "{s}"')
    if not strs: print('   (no string refs in window)')
