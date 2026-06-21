"""CORRECTED: real return addr = CreateInstance(0xda65e4) + 0x2e8d7d = 0x108f361.
Read the caller around it: the call just before 0x108f361 invoked the inner poll;
find the loop/exit structure."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe=pefile.PE(r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll",fast_load=True)
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; IB=0x180000000
RET=0x108f361
# Decode from a known-aligned point: back up and find clean alignment by decoding
# a wider window; mark the RET and the call above it.
START=RET-0xA0; END=RET+0xA0
data=pe.get_data(START,END-START)
print(f"=== real caller around 0x{RET:x} (RET = CreateInstance+0x2e8d7d) ===")
for insn in md.disasm(data, IB+START):
    rva=insn.address-IB
    note=""
    if rva==RET: note="  <== RETURN ADDR"
    elif abs(rva-RET)<6: note="  (near RET)"
    if insn.mnemonic=="call": note+="  [CALL]"
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        t=int(insn.op_str,16)-IB
        note+= f"  <== BACK-EDGE to 0x{t:x}" if t<rva else f"  -> 0x{t:x}"
    print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
