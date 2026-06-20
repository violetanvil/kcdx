"""Find rip-relative LEA xrefs into a set of target VAs (string anchors).

Usage: python find_lea_xrefs.py <WHGame.dll> <va_hex> [<va_hex> ...]
Reports each LEA instruction VA that loads the address of a target.
"""
import sys, struct
import pefile

DLL = sys.argv[1]
targets = {int(a, 16) for a in sys.argv[2:]}
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

text = None
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    if name == ".text":
        text = (image_base + sec.VirtualAddress, sec.get_data())
        break
text_va, text_data = text
n = len(text_data)

hits = {va: [] for va in targets}
i = 0
while True:
    j = text_data.find(b"\x8d", i)
    if j < 0 or j + 6 > n:
        break
    if j >= 1 and 0x48 <= text_data[j-1] <= 0x4f:
        modrm = text_data[j+1]
        if (modrm & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", text_data, j+2)[0]
            insn_va = text_va + (j-1)
            tgt = insn_va + 7 + disp
            if tgt in hits:
                hits[tgt].append(insn_va)
    i = j + 1

for va in targets:
    print(f"target 0x{va:X}: {len(hits[va])} LEA xref(s)")
    for ref in hits[va]:
        print(f"   LEA @ 0x{ref:X}")
