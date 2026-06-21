"""STATIC BATCH (no launch): read the OUTER loop around call site 0x667ddd.
The inner helper 0x865fb4 is bounded 5x; the infinite repetition must be the
outer frame re-running it. Find the outer loop's back-edge + exit condition.
We disasm a wide window around 0x667ddd and flag every backward jump (a back-edge
= the outer loop) and what it tests."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail=True
IB=0x180000000
# Wide window: 0x300 before the call .. 0x200 after, to catch the loop head + back-edge.
START=0x667ddd-0x300
END=0x667ddd+0x200
data=pe.get_data(START, END-START)
print(f"=== outer frame around call 0x667ddd (window 0x{START:x}..0x{END:x}) ===")
for insn in md.disasm(data, IB+START):
    rva=insn.address-IB
    note=""
    if rva==0x667ddd: note="  <== CALL inner poll 0x865fb4"
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        try:
            t=int(insn.op_str,16)-IB
            if t<rva and (rva-t)<0x400: note=f"  <== BACK-EDGE to 0x{t:x} (OUTER LOOP)"
        except: pass
    # flag calls to Sleep/Wait/GetActiveWindow imports
    print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
