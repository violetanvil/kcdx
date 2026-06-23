import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
base=pe.OPTIONAL_HEADER.ImageBase
secs=pe.sections
def rva_to_off(rva):
    for s in secs:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s.get_data()[rva-s.VirtualAddress:rva-s.VirtualAddress+0x200], s.Name.rstrip(b"\x00")
    return None,None
def cstr(va):
    rva=va-base
    data,_=rva_to_off(rva)
    if data is None: return None
    end=data.find(b"\x00")
    return data[:end].decode("latin1",errors="replace")
# strings
for label,lea_va in [("text r8 @0x23acaae (rip+0x23e521b)", 0x18183eca8+0 )]:
    pass
# compute lea targets precisely (addr_of_instr_end + disp)
print("lpText  (lea r8 @0x23acaae): instr end 0x23acab5 + 0x23e521b =", hex(0x18183eca8))
# Actually recompute: lea r8,[rip+0x23e521b] at VA 0x18183... -> rip = next instr VA
# raise window printed RVAs; convert: instr at RVA 0x23acaae, size 7 -> end RVA 0x23acab5
txt = base+0x23acab5+0x23e521b
print("lpText VA=",hex(txt)," = ", repr(cstr(txt)))
cap_msgbox_caption_note="rdx=rbx (caption passed in from caller; dynamic)"
# the earlier format-call strings (cmp byte / FUN_1804d4510 message builder)
s1=base+0x23ac9da+7+0x23e523f  # lea rcx,[rip+0x23e523f] at 0x23ac9da
s2=base+0x23ac9ce+7+0x23e52ab  # lea r8,[rip+0x23e52ab] at 0x23ac9ce
print("fmt rcx @0x23ac9da VA=",hex(s1),repr(cstr(s1)))
print("fmt r8  @0x23ac9ce VA=",hex(s2),repr(cstr(s2)))
s3=base+0x23aca36+7+0x23e52bb
s4=base+0x23aca4b+7+0x23e52be
print("lea rcx @0x23aca36 VA=",hex(s3),repr(cstr(s3)))
print("lea rcx @0x23aca4b VA=",hex(s4),repr(cstr(s4)))
# RaiseException wrapper at 0x1823dc960 -> disasm a few
text=next(s for s in secs if s.Name.rstrip(b"\x00")==b".text"); tdata=text.get_data(); tva=text.VirtualAddress
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; md.skipdata=True
raise_thunk=None
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        if (imp.name or b"")==b"RaiseException": raise_thunk=imp.address-base
def disw(rva,n,label):
    print(f"\n--- {label} @0x{rva:08x} ---")
    off=rva-tva
    for ins in md.disasm(tdata[off:off+n], base+rva):
        m=""
        if ins.mnemonic=="call" and ins.operands and ins.operands[0].type==3:
            tgt=ins.address+ins.size+ins.operands[0].mem.disp-base
            if tgt==raise_thunk: m="  <== RaiseException"
        print(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}{m}")
disw(0x23dc960,0x40,"raise_wrapper_0x23dc960")
# MessageBox call: [rip+0x1656888] at 0x23acaba -> IAT slot
mb_va=base+0x23acaba+6+0x1656888
print("\nMessageBox IAT slot VA=",hex(mb_va),"RVA=",hex(mb_va-base))
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        if imp.address==mb_va: print("   -> import:", (imp.name or b"").decode())
