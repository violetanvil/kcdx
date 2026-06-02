import pefile, re, sys

pe = pefile.PE("third-party-ghidra/WHGame.dll", fast_load=True)
pe.parse_data_directories(directories=[
    pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
image_base = pe.OPTIONAL_HEADER.ImageBase

# Gather .text bytes + its RVA range
text = None
for s in pe.sections:
    if s.Name.rstrip(b'\x00') == b'.text':
        text = s
        break
text_rva = text.VirtualAddress
text_data = text.get_data()  # raw section bytes
text_size = len(text_data)
print(f".text rva=0x{text_rva:X} vsize=0x{text.Misc_VirtualSize:X} rawlen=0x{text_size:X}")

# Build set of function ENTRY rvas from .pdata (RUNTIME_FUNCTION.BeginAddress)
entry_rvas = set()
try:
    for f in pe.DIRECTORY_ENTRY_EXCEPTION:
        entry_rvas.add(f.struct.BeginAddress)
    print(f".pdata function entries: {len(entry_rvas)}")
except Exception as e:
    print("pdata parse failed:", e)

def parse_aob(s):
    # returns (bytes_list, mask_list) where mask True = wildcard
    toks = s.split()
    b, m = [], []
    for t in toks:
        if t in ('??','?'):
            b.append(0); m.append(True)
        else:
            b.append(int(t,16)); m.append(False)
    return b, m

def find_all(aob):
    b, m = parse_aob(aob)
    n = len(b)
    hits = []
    # naive scan over .text
    rng = text_size - n
    i = 0
    data = text_data
    first = b[0]; firstwild = m[0]
    while i <= rng:
        if not firstwild and data[i] != first:
            i += 1; continue
        ok = True
        for j in range(n):
            if not m[j] and data[i+j] != b[j]:
                ok = False; break
        if ok:
            hits.append(text_rva + i)  # RVA of match
        i += 1
    return hits

candidates = {
    # name: aob
    "row5_outfit_callsite (cap-39 rewrites tail 44 8A F0)": "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
    "row7_callsite_005605BC": "48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02",
    "row8_callsite_00566040": "48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01",
    "luaL_openlibs_entry (current broken)": "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D",
    "luaL_openlibs_+5_window (past detour)": "57 48 83 EC 20 48 8B F9 48 8D 1D",
}

for name, aob in candidates.items():
    hits = find_all(aob)
    cnt = len(hits)
    entryflags = []
    for h in hits:
        is_entry = h in entry_rvas
        entryflags.append(f"0x{h:X}{'(ENTRY!)' if is_entry else '(interior)'}")
    nbytes = len(aob.split())
    print(f"\n{name}")
    print(f"  aob={aob}")
    print(f"  bytes={nbytes} count={cnt} hits={entryflags}")
