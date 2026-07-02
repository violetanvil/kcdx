"""KI-0012 — what is C_ModManager +0x48/+0x50/+0x58, and does the GRAPHICS
path read it?

OBSERVED (live PROBE-L within-0x68 byte diff, trustworthy):
  genuine +0x48/+0x50/+0x58 = three pointers INSIDE the genuine scanned-list
  range; kcdx = different/stale (kcdx has no scanned list and zeroed them).

STATIC FACTS so far (read from the binary this session):
  - ctor zero-inits +0x18..+0x58 (rdi=0), then calls SELECT.
  - SELECT writes NOTHING to the modMgr (only stack-frame + reads +0x18/+0x20/
    +0x30/+0x38).
  - SELECT's inner helpers (0xda1178, 0xda1294) operate on +0x10 (modsDir
    CryString) + stack-locals, NOT +0x48..+0x58.
  - The enabled-list walker FUN_19C6268 reads ONLY modMgr+0x30.
  - MOUNT reads ONLY +0x30/+0x38/+0x40.

So +0x48/+0x50/+0x58 is written by NEITHER ctor (just zeroes) NOR SELECT NOR
its helpers NOR MOUNT NOR the enabled-list walker. Yet the GENUINE object has
them non-zero (PROBE-L). => A LATER engine pass (post-SELECT) writes them.

This script finds:
  1. The modMgr getter chain: the global 0x492b8a8 + virtual+0xB8. WHO ELSE
     calls [global0x492b8a8 -> *][+0xB8] (the modMgr getter) besides
     FUN_19C6268? Sweep .text for the 3-instr getter pattern
     (mov rcx,[rip->0x492b8a8]; mov rax,[rcx]; call [rax+0xB8]) and for any
     consumer that, after getting the modMgr, reads [modMgr+0x48].
  2. EVERY write [reg+0x48] where reg could be a modMgr — too broad; instead
     find readers of [X+0x48] right after a 0x492b8a8 getter, and any function
     that takes a modMgr and reads +0x48 (the {ptr,count@+8,cap@+0xC} shape:
     a read of [r+0x48], then [r+0x50] as a 32-bit count, then [r+0x54]/[+0x4C]
     as cap).
  3. Locate the graphics copy helper: prompt says
     ffxFsr2ResourceIsNull+0x633120, a `mov rax,[rdx]; mov [rcx],rax; ret`.
     Find the ffxFsr2ResourceIsNull export RVA, add 0x633120, disasm there +
     its bounds-check caller {ptr,count@+8,cap@+0xC,stride 0x10}.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

pe = pefile.PE(DLL)  # full load for exports
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

GLOBAL_MODMGR = 0x492b8a8  # the gEnv-style global the modMgr getter loads


def disasm(label, rva, n):
    off = rva - text_va
    print("=" * 78)
    print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}")
    print("=" * 78)
    if off < 0 or off + n > len(text_data):
        print(f"   OUT OF .text (off={off:#x})"); print(); return
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
        if ins.mnemonic == "ret":
            break
    print()


# 1. Find every rip-relative reference to the modMgr global 0x492b8a8 in .text.
print("=" * 78)
print(f"== ALL .text rip-references to the modMgr global RVA {GLOBAL_MODMGR:#x}")
print("=" * 78)
needle_targets = []
for ins in md.disasm(text_data, base + text_va):
    if "rip" in ins.op_str and ins.mnemonic in ("mov", "lea"):
        try:
            d = ins.op_str.split("rip")[1].split("]")[0]
            disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
            eff = (ins.address + ins.size + disp) - base
            if eff == GLOBAL_MODMGR:
                needle_targets.append(ins.address)
        except Exception:
            pass
print(f"  [{len(needle_targets)} references to {GLOBAL_MODMGR:#x}]")
for a in needle_targets[:40]:
    print(f"    ref at VA {a:#x}  RVA {a-base:#x}")
print()

# 2. Locate the ffxFsr2ResourceIsNull export + the copy helper at +0x633120.
print("=" * 78)
print("== exports matching ffxFsr2/NGX (locate the copy-helper anchor)")
print("=" * 78)
fsr_rva = None
if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
    for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if exp.name:
            nm = exp.name.decode("ascii", "replace")
            if "Fsr2" in nm or "FSR2" in nm or "fsr2" in nm or "NGX_Update" in nm:
                print(f"    {nm}  RVA {exp.address:#x}")
                if nm == "ffxFsr2ResourceIsNull":
                    fsr_rva = exp.address
print()
if fsr_rva is not None:
    helper_rva = fsr_rva + 0x633120
    print(f"   ffxFsr2ResourceIsNull RVA={fsr_rva:#x}; +0x633120 = {helper_rva:#x}")
    disasm("graphics copy helper (ffxFsr2ResourceIsNull+0x633120)", helper_rva, 0x20)
else:
    print("   ffxFsr2ResourceIsNull export not found by that exact name.")
