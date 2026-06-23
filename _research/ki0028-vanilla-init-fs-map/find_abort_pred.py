import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
base=pe.OPTIONAL_HEADER.ImageBase
text=next(s for s in pe.sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
# Target VAs we want predecessors for: the abort epilogue lands at 0x18183ec85 (join),
# and the banner "PrepareLevel" code at 0x23ac994 is reached via jmp from 0x18183eb87 etc.
# These 0x18183eXXX are TRAMPOLINE addrs. Read them: what is at 0x18183ec85 / 0x18183eb87?
def disw(rva,n,label):
    print(f"\n--- {label} @0x{rva:08x} ---")
    off=rva-tva
    for ins in md.disasm(tdata[off:off+n], base+rva):
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}")
for rva,lab in [(0x18183ec85-base if False else 0x183ec85,"join_0x183ec85"),
                (0x183eb87,"tramp_0x183eb87"),
                (0x183ea7f,"tramp_0x183ea7f"),
                (0x183ea81,"tramp_0x183ea81"),
                (0x183ebc2,"tramp_0x183ebc2")]:
    disw(rva,0x30,lab)
