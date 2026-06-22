"""Linear-disasm a single function body from a known start to its terminal ret/int3 padding.
Seeded at the func start so capstone alignment is correct for that function.
Usage: python dump_func.py <func_rva_hex> [maxbytes]
"""
import sys, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
ts = base + text.VirtualAddress

rva = int(sys.argv[1], 16)
maxb = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x400
va = base + rva
off = va - ts
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
window = data[off: off + maxb]
prev_ret = False
for ins in md.disasm(window, va):
    print(f"{ins.address:#011x} (rva {ins.address-base:#08x})  {ins.mnemonic:<7} {ins.op_str}")
    if ins.mnemonic in ("ret", "retn") and prev_ret:
        pass
    # stop after a ret followed by int3 padding
    if ins.mnemonic == "ret":
        nxt_off = ins.address + ins.size - ts
        if nxt_off < len(data) and data[nxt_off] == 0xCC:
            print("  --- ret + int3 pad (func end) ---")
            break
