"""Find the loop that CONTAINS 0x667ddd. Disasm forward from 0x667ddd until a
back-edge whose target is <= 0x667ddd (the loop tail jumping back over our call),
and report that target (the loop head) + the branch condition that exits."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True); md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True
IB=0x180000000
# Scan forward from the call to the first backward jump landing <= 0x667ddd.
data=pe.get_data(0x667ddd, 0x600)
backedge=None
for insn in md.disasm(data, IB+0x667ddd):
    rva=insn.address-IB
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        t=int(insn.op_str,16)-IB
        if t<=0x667ddd:
            backedge=(rva,t,insn.mnemonic,insn.op_str); break
print(f"back-edge containing 0x667ddd: {backedge}")
if backedge:
    head=backedge[1]
    print(f"\n=== loop HEAD 0x{head:x} (the exit test sits at/just before the back-edge 0x{backedge[0]:x}) ===")
    # disasm from head, and from just before the back-edge, to read the exit condition
    d2=pe.get_data(head, 0x40)
    for insn in md.disasm(d2, IB+head):
        rva=insn.address-IB
        if rva>head+0x38: break
        print(f"HEAD 0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}")
    print("   ...")
    tail_start=backedge[0]-0x40
    d3=pe.get_data(tail_start, 0x50)
    for insn in md.disasm(d3, IB+tail_start):
        rva=insn.address-IB
        mark="BACKEDGE>>>" if rva==backedge[0] else "   "
        note=""
        if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
            t=int(insn.op_str,16)-IB
            if t>rva: note=f"  -> EXIT 0x{t:x}"
        print(f"{mark} 0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
