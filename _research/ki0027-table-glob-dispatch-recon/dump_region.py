"""Dump null-terminated strings in a VA window + list LEA xrefs into a set of VAs.

Usage: python dump_region.py <WHGame.dll> <start_va_hex> <end_va_hex>
"""
import sys, struct
import pefile

DLL = sys.argv[1]
start = int(sys.argv[2], 16)
end = int(sys.argv[3], 16)
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

secs = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    sva = image_base + sec.VirtualAddress
    data = sec.get_data()
    secs.append((name, sva, data))

def sec_for(va):
    for name, sva, data in secs:
        if sva <= va < sva + len(data):
            return name, sva, data
    return None

s = sec_for(start)
if not s:
    print("no section for start VA"); sys.exit(1)
name, sva, data = s
off0 = start - sva
off1 = min(end - sva, len(data))
i = off0
print(f"--- strings in [0x{start:X}..0x{end:X}] section {name} ---")
while i < off1:
    if data[i] == 0:
        i += 1
        continue
    j = i
    while j < off1 and data[j] != 0:
        j += 1
    raw = data[i:j]
    txt = "".join(chr(c) if 32 <= c < 127 else "." for c in raw)
    va = sva + i
    print(f"  0x{va:X}: {txt!r}")
    i = j + 1
