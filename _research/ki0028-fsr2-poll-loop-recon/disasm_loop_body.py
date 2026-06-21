"""Disasm the FSR2 poll-loop function from its clean prologue through past the
SleepEx return site (0x866090), to read the LOOP BACK-EDGE and EXIT TEST."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
def rva_to_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
START = 0x865fb4   # clean prologue of the SleepEx-caller function
END   = 0x866110   # past the SleepEx return site 0x866090
TARGET= 0x866090
data = pe.get_data(START, END - START)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
IB = 0x180000000
for insn in md.disasm(data, IB + START):
    rva = insn.address - IB
    mark = ">>>" if abs(rva - TARGET) < 6 else ("CALLSLEEP" if insn.mnemonic=="call" and "Sleep" in insn.op_str else "   ")
    print(f"{mark} 0x{rva:08x}  {insn.mnemonic:<8}{insn.op_str}")
