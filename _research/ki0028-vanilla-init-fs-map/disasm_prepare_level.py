import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
base=pe.OPTIONAL_HEADER.ImageBase
text=next(s for s in pe.sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
def cstr(va):
    rva=va-base
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            d=s.get_data(); o=rva-s.VirtualAddress; e=d.find(b"\x00",o)
            return d[o:e].decode("latin1","replace")
    return None
def find_start(rva,maxback=0x1200):
    i=rva-tva
    while i>(rva-tva)-maxback and i>1:
        if tdata[i-1]==0xCC: return tva+i
        i-=1
    return tva+(rva-tva-maxback)
# The PrepareLevel function: ret-into-epilogue is around 0x23ac994; the whole CET task
# is large. Walk back from 0x23ac994 to int3 start, disasm forward, annotate strings/calls.
start=find_start(0x23ac994)
print("func_start=0x%08x"%start)
off=start-tva
n=0x23acad0-start
out=[]
for ins in md.disasm(tdata[off:off+n], base+start):
    ann=""
    # rip-rel lea -> maybe a string
    if ins.mnemonic=="lea" and ins.operands and len(ins.operands)==2 and ins.operands[1].type==3:
        tgt=ins.address+ins.size+ins.operands[1].mem.disp
        s=cstr(tgt)
        if s and s.isprintable() and len(s)>3: ann="  ; \""+s[:70]+"\""
    out.append(f"0x{ins.address-base:08x}: {ins.mnemonic:<8} {ins.op_str}{ann}")
print("\n".join(out))
