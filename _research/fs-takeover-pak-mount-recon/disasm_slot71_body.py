"""fs-takeover pak-mount recon — Q1: slot 71 OpenPack/mount (FUN_1807ad468, RVA 0x7ad468) ABI + body.

Reads the decompiled-equivalent disassembly of the slot-71 function the prompt + front-1
name as OpenPack/mount, to resolve:
  - arg count/types (member __fastcall, rcx=this)
  - return type (does it set eax/rax meaningfully?)
  - what it operates on: does it touch the loaded-pak vector [this+0x120..+0x128]?
    does it CreateFile/fopen the pak + parse ZipDir? what does it return (index/bool/handle)?

AP2: ABI from the body, never prologue-guess. AP19: caller-edges read in caller bodies (separate script).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True


def disasm(label, rva, n):
    off = rva - text_va
    print("=" * 86)
    print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}  (bytes={n:#x})")
    print("=" * 86)
    if off < 0 or off + n > len(text_data):
        print("   OUT OF .text"); print(); return
    for ins in md.disasm(text_data[off:off + n], base + rva):
        note = ""
        if "rip" in ins.op_str and ins.mnemonic in ("lea", "mov", "call", "cmp", "movzx"):
            try:
                d = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                tgt = (ins.address + ins.size + disp) - base
                note = f"   -> RVA {tgt:#x}"
            except Exception:
                pass
        # absolute call/jmp targets within .text
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            try:
                tgt = int(ins.op_str, 16) - base
                note = f"   -> RVA {tgt:#x}"
            except Exception:
                pass
        print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}{note}")
    print()


# Slot 71 OpenPack/mount candidate — read generously to capture the full body + return path.
disasm("SLOT 71 candidate FUN_1807ad468 (prompt+front1 OpenPack/mount)", 0x7ad468, 0x600)

# Its leaf-call family front1 named (pak-open family) — short heads to characterize.
disasm("leaf FUN_1807ad6d4", 0x7ad6d4, 0xA0)
disasm("leaf FUN_1807ad76c", 0x7ad76c, 0xA0)
disasm("leaf FUN_1807ae228", 0x7ae228, 0xA0)
