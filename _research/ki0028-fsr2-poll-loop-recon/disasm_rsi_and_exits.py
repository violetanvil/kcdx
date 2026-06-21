"""Re-read the poll loop against the user's fact: the window DID get focus (mouse
cursor changed on click) yet the loop still hung. So GetActiveWindow==rsi is NOT
the (only) gate. Find: (1) what rsi IS (where the caller sets it before 0x866023),
(2) what the manager-method exits return. Disasm the caller 0x667ddd frame to see
how rsi is set going into 0x865fb4, and re-list the inner loop's exits precisely."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
IB = 0x180000000
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True

# The inner fn 0x865fb4 sets rsi itself? Re-disasm from prologue, tracking rsi.
print("=== inner fn 0x865fb4: how is rsi (the cmp operand) established? ===")
chunk = pe.get_data(0x865fb4, 0x90)
for insn in md.disasm(chunk, IB+0x865fb4):
    rva = insn.address - IB
    if rva >= 0x866030: break
    tag = ""
    if "rsi" in insn.op_str: tag = "   <-- rsi"
    print(f"0x{rva:08x}  {insn.mnemonic:<8}{insn.op_str}{tag}")

print("\n=== the caller 0x667ddd: what is rsi/rcx going INTO the call? (rcx=rsi set at 0x667dda) ===")
chunk2 = pe.get_data(0x667d50, 0xA0)
for insn in md.disasm(chunk2, IB+0x667d50):
    rva = insn.address - IB
    tag = "  <-- CALL poll fn" if rva==0x667ddd else ""
    if "rsi" in insn.op_str or "rcx" in insn.op_str or rva==0x667ddd:
        print(f"0x{rva:08x}  {insn.mnemonic:<8}{insn.op_str}{tag}")
