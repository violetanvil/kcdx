"""KI-0027 step 1 — string anchors for the table-DB loader + the __*.xml override glob.

Scan .rdata/.data for table-system strings and the override-merge / glob markers
so step 2 can LEA-xref them to the loader function.

Usage: python find_table_strings.py <WHGame.dll>
"""
import sys
import pefile

DLL = sys.argv[1]
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

NEEDLES = [
    b"tables can't be loaded",
    b"Database system error",
    b"Tables.pak",
    b"tables.pak",
    b"Libs/Tables",
    b"Libs\\Tables",
    b"Libs/Tables/",
    b".xml",
    b"__",            # the table-merge override suffix marker (will be noisy; filter later)
    b"DataTable",
    b"datatable",
    b"TableManager",
    b"CTableManager",
    b"LoadTable",
    b"LoadTables",
    b"GameData",
    b"Database",
    b"reading table",
    b"table file",
    b"merge",
    b"override",
    b"*.xml",
    b"__*.xml",
    b"*",
]

sections = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    sva = image_base + sec.VirtualAddress
    data = sec.get_data()
    sections.append((name, sva, data))

print(f"image_base = 0x{image_base:X}")
for name, sva, data in sections:
    print(f"  section {name:10s} VA 0x{sva:X} .. 0x{sva+len(data):X}")
print()

for needle in NEEDLES:
    all_hits = []
    for name, sva, data in sections:
        if name not in (".rdata", ".data", "_RDATA"):
            continue
        off = 0
        while True:
            k = data.find(needle, off)
            if k < 0:
                break
            va = sva + k
            ctx = data[max(0, k-8):k+len(needle)+28]
            ctx_s = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
            all_hits.append((va, name, ctx_s))
            off = k + 1
    # __ and .xml and * are extremely noisy; cap and only show ones with table/xml context
    show = all_hits
    if needle in (b"__", b".xml", b"*"):
        show = [h for h in all_hits if "table" in h[2].lower() or "Table" in h[2] or "Libs" in h[2]]
    print(f"=== {needle!r}: {len(all_hits)} hit(s){' (filtered)' if show is not all_hits else ''} ===")
    for va, sname, ctx in show[:30]:
        print(f"  0x{va:X} [{sname}]  {ctx}")
    print()
