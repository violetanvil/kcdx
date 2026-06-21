"""Read the REAL caller at 0xf8f361 (= CreateInstance+0x2e8d7d, the ground-truth
return addr from Main's stack). cdb's 'CreateInstance' base is RVA 0xda65e4
(verified export). Find the call to the helper just before 0xf8f361, and the
loop/exit structure around it."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe=pefile.PE(r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll",fast_load=True)
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True; IB=0x180000000
RET=0xf8f361   # return address into the real caller
START=RET-0x80; END=RET+0x80
data=pe.get_data(START,END-START)
print(f"=== real caller around 0x{RET:x} (the call BEFORE it invokes the inner poll) ===")
for insn in md.disasm(data, IB+START):
    rva=insn.address-IB
    note=""
    if rva==RET: note="  <== RETURN ADDR (call just above this invoked the inner poll)"
    if insn.mnemonic=="call": note+="  [CALL]"
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        t=int(insn.op_str,16)-IB
        if t<rva: note+=f"  <== BACK-EDGE to 0x{t:x}"
        else: note+=f"  -> 0x{t:x}"
    print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
