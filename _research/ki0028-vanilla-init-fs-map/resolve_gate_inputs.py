import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
base=pe.OPTIONAL_HEADER.ImageBase
text=next(s for s in pe.sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
def cstr(va):
    rva=va-base
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            d=s.get_data(); o=rva-s.VirtualAddress; e=d.find(b"\x00",o)
            try: return d[o:e].decode("latin1","replace")
            except: return None
def sec_of(va):
    rva=va-base
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode()
    return "?"
# globals
g1=base+0x183eb62+0x3c5753e  # WAIT recompute: mov rcx,[rip+0x3c5753e] at 0x183eb5b, size 7 -> rip=0x183eb62
print("global @[rip+0x3c5753e] (mov rcx @0x183eb5b): VA=0x%x sec=%s"%(g1, sec_of(g1)))
g2=base+0x183ebbc+0x30ece7c  # cmp byte [rip+0x30ece7c] at 0x183ebb5 size7 -> rip 0x183ebbc
print("flag   @[rip+0x30ece7c] (cmp byte @0x183ebb5): VA=0x%x sec=%s"%(g2, sec_of(g2)))
def disw(rva,n,label):
    print(f"\n--- {label} @0x{rva:08x} ---")
    off=rva-tva
    for ins in md.disasm(tdata[off:off+n], base+rva):
        ann=""
        for op in (ins.operands if ins.mnemonic not in (".byte",) else []):
            if op.type==3 and op.mem.base==25:
                tgt=ins.address+ins.size+op.mem.disp; s=cstr(tgt)
                if s and s.isprintable() and 3<len(s)<90: ann='  ; "'+s+'"'
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{ann}")
disw(0x66bbf0,0x60,"getter_0x66bbf0 (returns level/pak obj)")
disw(0x183ecac,0x50,"helper_0x183ecac (build from obj)")
disw(0x4fd468,0x50,"strfind_0x4fd468")
# confirm the abort epilogue banner at 0x23ac994
disw(0x23ac994,0x40,"abort_epilogue_0x23ac994")
