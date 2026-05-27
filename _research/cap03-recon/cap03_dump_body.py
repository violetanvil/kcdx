"""Dump a longer disassembly of a candidate to understand what it does."""
import sys
import pefile
import capstone

dll_path = sys.argv[1]
target_va = int(sys.argv[2], 16)
n_insns = int(sys.argv[3]) if len(sys.argv) > 3 else 80

pe = pefile.PE(dll_path, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
    if not (sec.Characteristics & 0x20000000):
        continue
    sec_va = image_base + sec.VirtualAddress
    sec_data = sec.get_data()
    if sec_va <= target_va < sec_va + len(sec_data):
        off = target_va - sec_va
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = False
        sub = sec_data[off:off + 4096]
        for i, ins in enumerate(md.disasm(sub, target_va)):
            print(f"  0x{ins.address:016X}: {ins.mnemonic:<8} {ins.op_str}")
            if i >= n_insns:
                break
            if ins.mnemonic in ("ret", "retn"):
                print("  --- ret ---")
                break
        break
