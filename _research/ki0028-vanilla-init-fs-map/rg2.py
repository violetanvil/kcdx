import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase
text=next(s for s in pe.sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True
def cstr(va):
    rva=va-base
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            d=s.get_data(); o=rva-s.VirtualAddress; e=d.find(b"\x00",o)
            try: return d[o:e].decode("latin1","replace")
            except: return None
def disw(rva,n,label):
    print(f"\n--- {label} @0x{rva:08x} ---")
    off=rva-tva
    for ins in md.disasm(tdata[off:off+n], base+rva):
        if ins.address-base>=rva+n: break
        ann=""
        try:
            for op in ins.operands:
                if op.type==3 and op.mem.base==25:
                    tgt=ins.address+ins.size+op.mem.disp; s=cstr(tgt)
                    if s and s.isprintable() and 3<len(s)<90: ann='  ; "'+s+'"'
        except Exception: pass
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{ann}")
disw(0x183ecac,0x60,"helper_0x183ecac")
disw(0x4fd468,0x60,"strfind_0x4fd468")
disw(0x23ac994,0x40,"abort_epilogue_0x23ac994")
