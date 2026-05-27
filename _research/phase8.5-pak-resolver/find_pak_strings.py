"""Phase 8.5a: locate the CryEngine pak/file resolver in WHGame.dll.

Step 1 — string anchors. Scan .rdata/.data for pak-subsystem strings and
CryPak RTTI type descriptors; report their VAs so the next pass can xref them.

Usage: python find_pak_strings.py <WHGame.dll>
Outputs VA + the containing-section name for each interesting hit.
"""
import sys
import pefile

DLL = sys.argv[1]
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

# Needles (case-sensitive byte search). RTTI type descriptors for CryEngine
# classes are stored as the mangled name `.?AV<Class>@@` in .data.
NEEDLES = [
    b"ICryPak",
    b"CCryPak",
    b"CryPak",
    b"OpenResource",
    b"FOpen",
    b"FGetSize",
    b"GetFileSize",
    b"exec autoexec.cfg",   # known gEnv anchor (cross-check we can find strings)
    b".pak",
    b"Localization/",
    b"AdjustFileName",
    b"IsFileExist",
    b"mkdir",
    b".?AVICryPak",
    b".?AVCCryPak",
    b"@CCryPak@",
    b"PakVars",
    b"OpenPacks",
    b"pakFile",
    b"Cannot open pak",
    b"missing pak",
]

sections = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    sva = image_base + sec.VirtualAddress
    data = sec.get_data()
    sections.append((name, sva, data))

def section_of(va):
    for name, sva, data in sections:
        if sva <= va < sva + len(data):
            return name
    return "?"

print(f"image_base = 0x{image_base:X}")
for name, sva, data in sections:
    print(f"  section {name:10s} VA 0x{sva:X} .. 0x{sva+len(data):X} ({len(data)} bytes)")
print()

for needle in NEEDLES:
    hits = []
    start = 0
    while True:
        i = -1
        for name, sva, data in sections:
            j = data.find(needle, 0)
            # collect all occurrences in this section
        # re-do per section to gather all
        break
    # gather all hits across sections
    all_hits = []
    for name, sva, data in sections:
        off = 0
        while True:
            k = data.find(needle, off)
            if k < 0:
                break
            va = sva + k
            # show a little context (the surrounding ASCII)
            ctx = data[max(0,k-4):k+len(needle)+24]
            ctx_s = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
            all_hits.append((va, name, ctx_s))
            off = k + 1
    print(f"=== {needle!r}: {len(all_hits)} hit(s) ===")
    for va, sname, ctx in all_hits[:40]:
        print(f"  0x{va:X} [{sname}]  {ctx}")
    print()
