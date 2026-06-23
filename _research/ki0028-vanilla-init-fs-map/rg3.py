import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL=r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe=pefile.PE(DLL,fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase
def sec_of(va):
    rva=va-base
    for s in pe.sections:
        if s.VirtualAddress<=rva<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode()
    return "?"
# resolve the two fatal-gate globals precisely
# 0x183ebb5: cmp byte [rip+0x30ece7c]  size 7 -> rip 0x183ebbc
print("flagA (0x183ebb5 cmp byte [rip+0x30ece7c]) VA=0x%x sec=%s"%(base+0x183ebbc+0x30ece7c, sec_of(base+0x183ebbc+0x30ece7c)))
# 0x23ac9be: cmp byte [rip+0x257f20c] size 7 -> rip 0x23ac9c5
print("flagB (0x23ac9be cmp byte [rip+0x257f20c]) VA=0x%x sec=%s"%(base+0x23ac9c5+0x257f20c, sec_of(base+0x23ac9c5+0x257f20c)))
# the getter's global obj base 0x1854960a0; what is at [+0x88] then [+0x58]? data section, can't read live, but note the chain.
# the vcall gate before MessageBox: 0x23aca9e call [rax+0x660]; the obj @[rip+0x257ee25] (0x23ac994.. ) :
# 0x23aca94: mov rcx,[rip+0x257ee25] size7 -> rip 0x23aca9b
print("gateObj (0x23aca94 mov rcx [rip+0x257ee25]) VA=0x%x sec=%s"%(base+0x23aca9b+0x257ee25, sec_of(base+0x23aca9b+0x257ee25)))
# 0x23aca57: mov rax,[r15]; call [rax+0xa8]; r15 set where? and 0x23aca67 obj [rip+0x257ee52]
print("obj2 (0x23aca67 mov rcx [rip+0x257ee52]) VA=0x%x sec=%s"%(base+0x23aca6e+0x257ee52, sec_of(base+0x23aca6e+0x257ee52)))
