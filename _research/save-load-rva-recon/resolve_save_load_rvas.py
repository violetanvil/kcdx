# Resolve the 5 save/load AOB locators -> unique RVAs against WHGame.dll, and
# confirm each resolved RVA is present in the dump's functions/ table (the
# content_hash/length PROMOTE dependency for a new kind=function seed row).
#
# Byte-match primitives (parse_aob / count_text_matches) are COPIED from
# _research/seed-verification-recon/verify_seeds.py (the seed-maintainer's
# verified scanner) — not re-derived. Image base 0x180000000, KCD2 1.5.1164953.
import os, csv, glob

import pefile

REPO = r"c:/Users/Michael/Documents/KCD2 Mods/kcdx"
DLL = os.path.join(REPO, "third-party-ghidra", "WHGame.dll")
DUMP_FUNCS = os.path.join(REPO, "data/refdata-extractor/dump/refdata-1.5.1164953/functions")
IMAGE_BASE = 0x180000000

pe = pefile.PE(DLL, fast_load=True)
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1")
    va = s.VirtualAddress
    vsize = max(s.Misc_VirtualSize, s.SizeOfRawData)
    sections.append((va, va + vsize, s.PointerToRawData, name, s.SizeOfRawData))

with open(DLL, "rb") as f:
    DATA = f.read()

text_rng = None
for va, end, raw, name, rawsz in sections:
    if name == ".text":
        text_rng = (raw, raw + rawsz, va)
TEXT_RAW_START, TEXT_RAW_END, TEXT_VA = text_rng
TEXT_BYTES = DATA[TEXT_RAW_START:TEXT_RAW_END]


def parse_aob(aob):
    """Parse 'AA BB ?? CC' into (bytes, mask) -- ?? = wildcard. (from verify_seeds.py)"""
    toks = aob.strip().split()
    b = bytearray()
    m = bytearray()
    for t in toks:
        if t in ("??", "?"):
            b.append(0)
            m.append(0)
        else:
            b.append(int(t, 16))
            m.append(0xFF)
    return bytes(b), bytes(m)


def count_text_matches(aob):
    """Scan .text for the AOB; return (count, first_hit_RVA). (from verify_seeds.py)"""
    b, m = parse_aob(aob)
    n = len(b)
    cnt = 0
    first = None
    rng = len(TEXT_BYTES) - n
    i = 0
    while i <= rng:
        ok = True
        for j in range(n):
            if (TEXT_BYTES[i + j] & m[j]) != (b[j] & m[j]):
                ok = False
                break
        if ok:
            cnt += 1
            if first is None:
                first = TEXT_VA + i  # RVA (TEXT_VA is the section's RVA)
            if cnt > 5:
                break
        i += 1
    return cnt, first


# Load every dump function RVA (hex string -> int) for the presence check.
dump_rvas = {}
for path in glob.glob(os.path.join(DUMP_FUNCS, "functions_*.csv")):
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            try:
                dump_rvas[int(row["rva"], 16)] = (row["length"], row["content_hash"])
            except (KeyError, ValueError):
                continue

TARGETS = [
    ("SaveGame",
     "4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 "
     "48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6"),
    ("LoadGame_wrapper",
     "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B D8 8B FA 48 8B F1 E8 "
     "C0 F1 FF FF 48 8D 8E C8 00 00 00 66 C7 86 C0 00"),
    ("PostLoadGame",
     "48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 "
     "49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8"),
    ("DeleteSavegame",
     "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 "
     "48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF"),
    ("SaveGameRecord_SlotResolver",
     "48 63 C2 48 8D 14 C0 48 8D 0C D1 41 8B D0 48 83 C1 08 E9 7D 5D D2 FE"),
]

print(f"{'name':<30} {'count':>5} {'rva':>12} {'VA':>14} {'in_dump':>8} {'dump_len':>8}")
print("-" * 84)
for name, aob in TARGETS:
    cnt, first = count_text_matches(aob)
    rva_s = f"0x{first:08X}" if first is not None else "(none)"
    va_s = f"0x{IMAGE_BASE + first:X}" if first is not None else "-"
    in_dump = "YES" if (first is not None and first in dump_rvas) else "NO"
    dump_len = dump_rvas[first][0] if (first is not None and first in dump_rvas) else "-"
    uniq = "UNIQUE" if cnt == 1 else f"!!{cnt}!!"
    print(f"{name:<30} {uniq:>5} {rva_s:>12} {va_s:>14} {in_dump:>8} {dump_len:>8}")
