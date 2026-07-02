"""KI-0012 — context around FUN_182113a60: who takes its address, what it builds.

FUN_182113a60 writes *rcx=rdx then inits [rcx+8] — a placement-ctor shape. Its
address is LEA'd at two sites (0x2113d18, 0x2114ff5). Disassemble those callers
+ the sub-init 0x182112ee0 to learn what object class it constructs and whether
its address ends up at a modMgr +0x70 slot.

Also: the native ctor allocates 0x68 and never writes +0x70. PROBE K saw the
genuine object's +0x70 = 0x2113A60 (this function's OWN address). Test the
heap-adjacency hypothesis: is 0x68-from-some-base == the start of a DIFFERENT
object whose +0x00 vtable/field holds 0x2113A60? If FUN_182113a60's address sits
in a vtable, the genuine 'pointer at +0x70' could be a NEIGHBORING object's
vtable slot read past the 0x68 modMgr.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

TARGETS = {
    "caller_A@RVA_2113D18 (lea rax,FUN_2113A60)": (0x2113C80, 0x140),
    "caller_B@RVA_2114FF5 (lea rax,FUN_2113A60)": (0x2114F60, 0x140),
    "sub_init@RVA_2112EE0 (called by FUN_2113A60)": (0x2112EE0, 0xA0),
}

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    rdata = next((s for s in pe.sections if s.Name.rstrip(b"\x00") == b".rdata"), None)
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
            print(f"   OUT OF .text (off={off})"); print(); continue
        body = text_data[off:off + nbytes]
        for ins in md.disasm(body, base + rva):
            note = ""
            if "rip" in ins.op_str and ins.mnemonic in ("lea", "mov", "call"):
                try:
                    d = ins.op_str.split("rip")[1].split("]")[0]
                    disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                    eff = ins.address + ins.size + disp
                    note = f"   -> RVA {eff-base:#x}"
                except Exception:
                    pass
            print(f"  {ins.address:#010x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}")
        print()

    # Is RVA 0x2113A60 stored anywhere in .rdata as a qword (i.e. sitting in a
    # vtable / function-pointer table)? Search .rdata for the VA bytes.
    print("=" * 78)
    print("== .rdata qword occurrences of VA 0x182113A60 (would mean it's in a vtable/fnptr table)")
    print("=" * 78)
    if rdata:
        rdata_data = rdata.get_data()
        rdata_va = rdata.VirtualAddress
        needle = (base + 0x2113A60).to_bytes(8, "little")
        idx = 0
        hits = 0
        while True:
            i = rdata_data.find(needle, idx)
            if i < 0:
                break
            slot_rva = rdata_va + i
            print(f"  .rdata RVA {slot_rva:#x}  (qword holds VA 0x182113A60)")
            hits += 1
            idx = i + 1
            if hits > 30:
                print("  ... capped"); break
        print(f"   [.rdata qword hits: {hits}]")
    else:
        print("   (no .rdata section found)")

if __name__ == "__main__":
    main()
