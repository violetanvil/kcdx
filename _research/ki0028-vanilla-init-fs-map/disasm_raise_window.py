import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00")==b".text")
tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
raise_thunk=None
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        nm=(imp.name or b"").decode(errors="replace")
        if nm=="RaiseException": raise_thunk=imp.address-base
print("RaiseException thunk RVA=0x%08x (VA 0x%x)"%(raise_thunk, base+raise_thunk))

def win(center, before, after, label):
    start=center-before
    off=start-tva
    print(f"\n===== {label}: window 0x{start:08x}..0x{center+after:08x} =====")
    for ins in md.disasm(tdata[off:off+before+after], base+start):
        mark=""
        if ins.mnemonic.startswith("call"):
            # resolve direct call target to thunk?
            if ins.operands and ins.operands[0].type==3:  # mem (rip-rel)
                disp=ins.operands[0].mem.disp
                tgt=ins.address+ins.size+disp
                if tgt-base==raise_thunk: mark="  <== call RaiseException"
            elif ins.operands and ins.operands[0].type==2:  # imm
                if ins.operands[0].imm-base==raise_thunk: mark="  <== RaiseException"
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<8} {ins.op_str}{mark}")

# the ret addr 0x23acaca => call is just before; widen window
win(0x23acaca, 0x140, 0x10, "raise_site_around_0x23acaca")
