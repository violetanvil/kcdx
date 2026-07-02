"""KI-0012 — resolve the C_ModManager +0x70 field + the true alloc size.

Reuse-first ladder exhausted at tier 2 for FUN_182113a60 (inventory stub only,
prior _research dumps don't cover it). Tier 5: fresh capstone disassembly.

Questions:
  1. Does the ctor allocator (0x4f7820) read its size from rcx? (confirm ecx=0x68
     is the alloc size, not a coincidence.)
  2. What is FUN_182113a60 (RVA 0x2113A60), the value PROBE K saw at the genuine
     object's +0x70?
  3. Does the AddCommand wrapper (0xb99098) write to [modMgr+0x70]?
  4. Is 0x2113A60 referenced as a vtable slot / via LEA anywhere near the ctor or
     the C_ModManager vtable (RVA 0x3AA2E60)?
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

# Disassemble these (rva, nbytes) bodies fully.
TARGETS = {
    "FUN_182113a60@RVA_2113A60 (the +0x70 value)": (0x2113A60, 0x120),
    "AddCommand_wrapper@RVA_B99098 (ctor call 4)": (0xB99098, 0x100),
    "allocator@RVA_4F7820 (confirm rcx=size)":     (0x4F7820, 0x50),
}

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    text_data = text.get_data()
    text_va = text.VirtualAddress

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.skipdata = True

    for label, (rva, nbytes) in TARGETS.items():
        off = rva - text_va
        print("=" * 78)
        print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}")
        print("=" * 78)
        if off < 0 or off + nbytes > len(text_data):
            print(f"   RVA {rva:#x} OUT OF .text (off={off})")
            print()
            continue
        body = text_data[off:off + nbytes]
        va = base + rva
        writes_70 = False
        for ins in md.disasm(body, va):
            mark = ""
            # Flag any write to [reg+0x70].
            if ins.mnemonic.startswith("mov") and "+ 0x70]" in ins.op_str and "[" == ins.op_str.lstrip()[0:1]:
                mark = "   <<< WRITE TO [reg+0x70]"
                writes_70 = True
            print(f"  {ins.address:#010x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{mark}")
            if ins.mnemonic == "ret" and (ins.address - va) > 0x10:
                break
        print(f"   [writes +0x70: {writes_70}]")
        print()

    # Scan the whole .text for any LEA/MOV that loads the address 0x2113A60
    # (rip-relative) — i.e. who PRODUCES the +0x70 value as an immediate target.
    print("=" * 78)
    print("== rip-relative references TO RVA 0x2113A60 across .text (lea/mov reg,[rip+...])")
    print("=" * 78)
    target_rva = 0x2113A60
    hits = 0
    for ins in md.disasm(text_data, base + text_va):
        if ins.mnemonic in ("lea", "mov") and "rip" in ins.op_str:
            # capstone resolves rip-relative effective addresses
            try:
                disp_str = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(disp_str.replace("+", "").replace(" ", ""), 16) if disp_str.strip() else 0
            except Exception:
                continue
            eff = ins.address + ins.size + disp
            eff_rva = eff - base
            if eff_rva == target_rva:
                print(f"  {ins.address:#010x} (RVA {ins.address-base:#x})  {ins.mnemonic:<5} {ins.op_str}")
                hits += 1
                if hits > 40:
                    print("  ... (capped at 40)")
                    break
    print(f"   [total rip-relative refs to 0x2113A60: {hits}]")

if __name__ == "__main__":
    main()
