"""fs-takeover recon — robust indirect-call scan by raw byte pattern (no linear-disasm drift).

A vtable dispatch `call qword ptr [reg + disp32]` encodes as:
  FF /2 with mod=10 (disp32):
    no-SIB:  REX? FF [modrm: mod=10 reg=010 rm=reg]  disp32     (rm != 100, != 101)
    SIB:     REX? FF [modrm: mod=10 reg=010 rm=100] [sib] disp32
modrm byte for /2, mod=10: 0x90 | rm  (rm in 0..7), reg field already 010<<3=0x10.
So modrm = 0x90+rm. For each candidate, the 4 bytes after modrm (+sib if rm==4) == disp32.
We scan .text for FF (90..97) <disp32==target>, and FF 94 <sib> <disp32==target>, with an
optional REX prefix (40..4F) immediately before FF, and disassemble a window for context.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)

REG = {0: "rax", 1: "rcx", 2: "rdx", 3: "rbx", 4: "rsp/SIB", 5: "rbp", 6: "rsi", 7: "rdi"}


def find_sites(disp, label):
    print("=" * 86)
    print(f"== call qword ptr [reg + {disp:#x}]  ({label})")
    print("=" * 86)
    dbytes = disp.to_bytes(4, "little")
    n = len(data)
    hits = []
    for i in range(n - 7):
        if data[i] != 0xFF:
            continue
        modrm = data[i + 1]
        if (modrm & 0x38) != 0x10:  # reg field must be 010 (/2 = call r/m64)
            continue
        if (modrm & 0xC0) != 0x80:  # mod must be 10 (disp32)
            continue
        rm = modrm & 0x07
        if rm == 4:  # SIB form
            disp_off = i + 3
        elif rm == 5:  # rbp base, valid with disp32
            disp_off = i + 2
        else:
            disp_off = i + 2
        if data[disp_off:disp_off + 4] == dbytes:
            site = i
            rexnote = ""
            if i > 0 and 0x40 <= data[i - 1] <= 0x4F:
                site = i - 1
                rexnote = f" rex={data[i-1]:#04x}"
            va = base + tva + site
            hits.append((va, rm, rexnote))
    for va, rm, rexnote in hits:
        # disassemble the single instruction at the site for an exact mnemonic
        off = va - base - tva
        ins = next(md.disasm(data[off:off + 8], va), None)
        txt = f"{ins.mnemonic} {ins.op_str}" if ins else "<decode-fail>"
        print(f"  VA {va:#x}  RVA {va-base:#x}  base-reg~{REG.get(rm,'?')}{rexnote}   {txt}")
    print(f"  total: {len(hits)}")
    print()
    return [h[0] for h in hits]


find_sites(0x238, "slot 71  FUN_1807ad468")
find_sites(0x320, "slot 100 FUN_182418f78")
