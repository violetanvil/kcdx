"""KI-0012 — trace the {ptr,count@+8,cap@+0xC,stride 0x10} structure the
graphics copy helper (RVA 0xb2e220) faults iterating, and whether its base
comes from the C_ModManager.

The crash dump (prompt): rdx = a GARBAGE per-boot pointer passed to the copy
helper 0xb2e220. The caller iterates `element = [object_base] + index*0x10`
with bounds compare `[rbx+8]` (count) vs `[rdi+0x0C]` (cap). Find the direct
callers of 0xb2e220 and disasm them to read the structure shape + where
object_base comes from.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
HELPER_RVA = 0xb2e220

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True

helper_va = base + HELPER_RVA


def disasm(label, rva, n):
    off = rva - text_va
    print("=" * 78)
    print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}")
    print("=" * 78)
    if off < 0 or off + n > len(text_data):
        print(f"   OUT OF .text"); print(); return
    for ins in md.disasm(text_data[off:off + n], base + rva):
        note = ""
        if "rip" in ins.op_str and ins.mnemonic in ("lea", "mov", "call", "cmp"):
            try:
                d = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                note = f"   -> RVA {(ins.address+ins.size+disp)-base:#x}"
            except Exception:
                pass
        print(f"  {ins.address:#010x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}")
    print()


# Find direct E8 callers of the helper 0xb2e220.
print("=" * 78)
print(f"== direct E8 callers of the copy helper RVA {HELPER_RVA:#x}")
print("=" * 78)
callers = []
for ins in md.disasm(text_data, base + text_va):
    if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
        try:
            tgt = int(ins.op_str, 16)
        except ValueError:
            continue
        if tgt == helper_va:
            callers.append(ins.address)
print(f"  [{len(callers)} direct callers]")
for a in callers:
    print(f"    caller at VA {a:#x}  RVA {a-base:#x}")
print()

# Disasm each caller's surrounding region (0x60 before .. 0x10 after) so we see
# the {ptr,count@+8,cap@+0xC,stride 0x10} index math + bounds check.
for a in callers[:12]:
    rva = a - base
    disasm(f"caller-context of {rva:#x} (helper call site)", rva - 0x60, 0x80)
