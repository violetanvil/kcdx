"""Full disassembly of ctor + SELECT bodies (no filtering)."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

TARGETS = {
    "ModManager_ctor@180DA0EB0":   (0xDA0EB0, 0x200),
    "ModManager_Select@180DA104C": (0xDA104C, 0x300),
    # The first helper called by ctor — what is it?
    "helper@1804F7820":            (0x4F7820, 0x80),
    # The third helper called by ctor (between +0x08 write and zero-init)
    "helper@1804F692C":            (0x4F692C, 0x80),
    # Helper at SELECT's first big call
    "helper@180DA0FB0":            (0xDA0FB0, 0x150),
    # The big helper called from SELECT mid-body
    "helper@180DA1178":            (0xDA1178, 0x200),
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
        if off < 0 or off + nbytes > len(text_data):
            print(f"== {label} RVA {rva:#x} OUT OF .text"); continue
        body = text_data[off:off + nbytes]
        va = base + rva
        print("=" * 78)
        print(f"== {label}   VA={va:#x}  RVA={rva:#x}")
        print("=" * 78)
        for ins in md.disasm(body, va):
            print(f"  {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
            # Stop on first ret beyond 0x20 from start
            if ins.mnemonic == "ret" and (ins.address - va) > 0x20:
                break
        print()

if __name__ == "__main__":
    main()
