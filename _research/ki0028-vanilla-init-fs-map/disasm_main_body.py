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
    return None
def find_start(rva,maxback=0x600):
    i=rva-tva
    while i>(rva-tva)-maxback and i>1:
        if tdata[i-1]==0xCC and tdata[i-2]==0xCC: return tva+i
        i-=1
    return tva+(rva-tva-maxback)
def disrange(start,end,label):
    print(f"\n===== {label} 0x{start:08x}..0x{end:08x} =====")
    off=start-tva
    for ins in md.disasm(tdata[off:off+(end-start)], base+start):
        ann=""
        for op in ins.operands:
            if op.type==3 and op.mem.base==25:  # rip
                tgt=ins.address+ins.size+op.mem.disp
                s=cstr(tgt)
                if s and s.isprintable() and 3<len(s)<90: ann='  ; "'+s+'"'
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<8} {ins.op_str}{ann}")
# Main body region leading into the abort gate (the je 0x183ebc7 at 0x183ebb3 region back to entry)
fs=find_start(0x183eb87)
print("main_body_start≈0x%08x"%fs)
disrange(fs,0x183ec00,"PrepareLevel_main_body")
# The outer caller frame 01->02 = 0x66ba9c : read its tail (what it does with the CET result)
cs=find_start(0x66ba9c, 0x800)
disrange(cs,0x66ba9c+0x10,"outer_caller_0x66ba9c_tail")
