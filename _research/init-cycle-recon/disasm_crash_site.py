"""Disassemble around the ACTUAL crash site at RVA 0x244D085.

The previous SQL query may have been wrong about which function CONTAINS
the crash. 0x244D085 is in the .text but 0xC419 bytes past FUN_182440c6c's
start. Either FUN_182440c6c is gigantic, or 0x244D085 is in a DIFFERENT
function. Let me look at the raw bytes around the crash site AND query
the DB for any function whose [rva, rva+length) actually covers 0x244D085.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
CRASH_RVA = 0x244D085

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress

# Try to find a function start nearby by scanning backwards for typical
# x64 prologues. A function usually starts with one of:
#   48 89 5C 24 .. -- mov [rsp+x], rbx
#   48 8B C4    -- mov rax, rsp
#   40 53       -- push rbx
#   48 83 EC .. -- sub rsp, x  (after pushes)
#   55          -- push rbp
# Scan backwards from CRASH_RVA up to 0x1000 bytes looking for these.

off = CRASH_RVA - text_va
# Show 0x40 bytes BEFORE the crash + 0x40 bytes including/after.
window_lo = max(0, off - 0x40)
window_hi = min(len(text_data), off + 0x40)

md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True

# First scan backward 0x2000 bytes for `48 8B C4` (mov rax, rsp) as a
# function-start heuristic.
scan_start_off = max(0, off - 0x2000)
scan_data = text_data[scan_start_off:off]
print(f"Scanning {len(scan_data)} bytes BEFORE crash for function-prologue patterns:")
for i, sig in [("48 8B C4", b"\x48\x8b\xc4"), ("48 89 5C 24", b"\x48\x89\x5c\x24"),
               ("40 53", b"\x40\x53"), ("55 41 56", b"\x55\x41\x56")]:
    # Find the LAST occurrence before crash
    idx = scan_data.rfind(sig)
    if idx >= 0:
        rva = text_va + scan_start_off + idx
        delta = CRASH_RVA - rva
        print(f"  {i!r:12s} last seen at RVA {rva:#x}  ({delta} = {delta:#x} bytes before crash)")

print()
print(f"=== Disassembly window around crash (RVA {CRASH_RVA:#x}) ===")
# Try disassembling from 0x30 bytes BEFORE the crash to 0x40 bytes after.
disasm_start = max(0, off - 0x30)
disasm_data = text_data[disasm_start:off + 0x40]
disasm_va = base + text_va + disasm_start
for ins in md.disasm(disasm_data, disasm_va):
    marker = " <-- CRASH" if (ins.address - base) == CRASH_RVA else ""
    print(f"  {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}{marker}")
    if (ins.address - base) > CRASH_RVA + 0x30:
        break
