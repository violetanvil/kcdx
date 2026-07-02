"""KI-0012 — what struct holds the FUN_2113A60 function pointer, and at what
offset. Decides the heap-adjacency hypothesis for the genuine object's +0x70.

The two call_once initializers store FUN_2113A60's address into a stack struct
then call 0x180932b74 (rcx=a .data global, rdx=&struct{fnptr@+0?}, r8=&another).
Disassemble 0x180932b74 to see where it stores the fnptr (which offset of which
target object). If a global singleton gets FUN_2113A60 written at ITS +0x70,
and that singleton is allocated adjacent-after a 0x68 C_ModManager on the same
boot, the PROBE-K '+0x70 pointer' is a heap-neighbor read.

Also dump the alloc-size sweep: list EVERY call to the allocator 0x4f7820 and
the immediately-preceding size set into ecx/rcx, to catch any OTHER C_ModManager
allocation path with a size != 0x68.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    text_data = text.get_data()
    text_va = text.VirtualAddress
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.skipdata = True

    # 1. The registration helper 0x180932b74 — where does it put the fnptr?
    for label, rva, n in [
        ("reg_helper@RVA_932B74", 0x932B74, 0xC0),
        ("reg_helper_sibling@RVA_932B08", 0x932B08, 0xC0),
    ]:
        off = rva - text_va
        print("=" * 78)
        print(f"== {label}   RVA={rva:#x}")
        print("=" * 78)
        for ins in md.disasm(text_data[off:off+n], base+rva):
            note = ""
            if "rip" in ins.op_str:
                try:
                    d = ins.op_str.split("rip")[1].split("]")[0]
                    disp = int(d.replace("+","").replace(" ",""),16) if d.strip() else 0
                    note = f"   -> RVA {(ins.address+ins.size+disp)-base:#x}"
                except Exception: pass
            print(f"  {ins.address:#010x}  {ins.mnemonic:<7} {ins.op_str}{note}")
            if ins.mnemonic == "ret" and (ins.address-(base+rva)) > 0x10:
                break
        print()

    # 2. Sweep: every E8 call to the allocator 0x4f7820, with the preceding
    #    instructions that set the size. Catch any size != 0x68.
    print("=" * 78)
    print("== ALL call sites of allocator 0x4f7820 — preceding size set (lea ecx,[..N] / mov ecx,N)")
    print("=" * 78)
    alloc_va = base + 0x4F7820
    # collect linear disasm with a small look-back window
    insns = list(md.disasm(text_data, base + text_va))
    by_addr = {}
    for i, ins in enumerate(insns):
        by_addr[ins.address] = i
    count = 0
    for i, ins in enumerate(insns):
        if ins.mnemonic == "call" and ins.op_str:
            opv = ins.op_str
            if opv.startswith("0x"):
                try: tgt = int(opv, 16)
                except ValueError: tgt = 0
                if tgt == alloc_va:
                    count += 1
                    # look back up to 6 insns for an ecx/rcx set
                    ctx = insns[max(0,i-6):i+1]
                    sizenote = ""
                    for c in reversed(ctx[:-1]):
                        if ("ecx" in c.op_str or "rcx" in c.op_str) and c.mnemonic in ("lea","mov","xor"):
                            sizenote = f"  size<- {c.mnemonic} {c.op_str}"
                            break
                    print(f"  call@{ins.address:#x} (RVA {ins.address-base:#x}){sizenote}")
                    if count <= 12:
                        for c in ctx:
                            print(f"       {c.address:#x}  {c.mnemonic:<6} {c.op_str}")
                        print()
    print(f"   [total allocator call sites: {count}]")

if __name__ == "__main__":
    main()
