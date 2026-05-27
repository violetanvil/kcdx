"""Phase 6 RTTI + class-name anchor scanner.

Finds RTTI type descriptors (.?AU... / .?AV...) for save/load-related
classes and prints the surrounding strings to identify class names that
host SaveGame/LoadGame methods. Also dumps a focused C-string window
around a few high-value VAs from the loose-fit scan.

Usage:
    py phase6_rtti_and_class_anchors.py <path/to/WHGame.dll>
"""

import sys
from pathlib import Path

import pefile


# Class-name fragments to look for in RTTI tables. The compiler emits
# `.?AVClassName@@` for `class ClassName` and `.?AUStructName@@` for
# `struct StructName`. We look for any RTTI hit containing save/load/serialize.
RTTI_FRAGMENTS = [
    b"SaveGame",
    b"LoadGame",
    b"Serialize",
    b"savegame",
    b"loadgame",
    b"Save",   # broad
    b"Load",   # broad
    b"CryAction",
]


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: phase6_rtti_and_class_anchors.py <WHGame.dll>")
    dll_path = Path(sys.argv[1])
    if not dll_path.exists():
        sys.exit(f"not found: {dll_path}")

    pe = pefile.PE(str(dll_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    sections = {}
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        sections[name] = (sec_va, sec_data)

    rtti_sections = [".data", ".rdata"]

    print("=" * 78)
    print("RTTI TYPE DESCRIPTORS containing save/load/serialize/CryAction")
    print("=" * 78)

    seen = set()
    for sec_name in rtti_sections:
        if sec_name not in sections:
            continue
        sec_va, sec_data = sections[sec_name]
        # RTTI descriptor strings start with ".?A" (after one NUL byte of
        # type_info::__type_info_node padding-ish). Find every ".?A"
        # occurrence and read its C string forward.
        start = 0
        while True:
            idx = sec_data.find(b".?A", start)
            if idx == -1:
                break
            end = sec_data.find(b"\x00", idx, idx + 256)
            if end == -1:
                start = idx + 1
                continue
            s = sec_data[idx:end]
            for frag in RTTI_FRAGMENTS:
                if frag in s:
                    key = (s, sec_name)
                    if key not in seen:
                        seen.add(key)
                        print(f"  {sec_name}  VA=0x{sec_va + idx:016X}  {s!r}")
                    break
            start = idx + 1

    # Also dump C-string windows around the most interesting log strings
    # so we can read the format strings precisely.
    print()
    print("=" * 78)
    print("DETAILED CONTEXT for high-value log strings")
    print("=" * 78)

    detail_vas = [
        ("0x183E16978", 0x183E16978, "savegame description / SaveDescription"),
        ("0x183E16ADD", 0x183E16ADD, "in savegame system console var"),
        ("0x183E16B49", 0x183E16B49, "saving savegame %d of playline"),
        ("0x183E16D58", 0x183E16D58, "loaded savegame"),
        ("0x183E16DAA", 0x183E16DAA, "Newest savegame in playline"),
        ("0x183E16C00", 0x183E16C00, "Loading saved game ver. %d"),
        ("0x184725368", 0x184725368, "Loading last saved game"),
        ("0x18479499D", 0x18479499D, "Invalid save game version"),
        ("0x184793906", 0x184793906, "CRichSaveGameHelper:SkipXMLTagData"),
        ("0x184059C66", 0x184059C66, "Cannot save XML / CXMLSaveGameFSDir"),
    ]
    for name, target, label in detail_vas:
        # Find the .rdata or .data section containing target
        hit_sec = None
        for sec_name in (".rdata", ".data"):
            if sec_name not in sections:
                continue
            sec_va, sec_data = sections[sec_name]
            if sec_va <= target < sec_va + len(sec_data):
                hit_sec = (sec_va, sec_data, sec_name)
                break
        if not hit_sec:
            print(f"\n  {label}\n    {name}: NOT FOUND IN .rdata/.data")
            continue
        sec_va, sec_data, sec_name = hit_sec
        idx = target - sec_va
        # Walk back to nearest NUL (start of the C string) up to 64 bytes.
        s_start = idx
        for back in range(1, 65):
            if idx - back < 0:
                break
            if sec_data[idx - back] == 0:
                s_start = idx - back + 1
                break
        # Walk forward to nearest NUL (end of the C string) up to 256 bytes.
        s_end = sec_data.find(b"\x00", idx, idx + 256)
        if s_end == -1:
            s_end = idx + 60
        full = sec_data[s_start:s_end]
        s_full = "".join(chr(b) if 32 <= b < 127 else "?" for b in full)
        print(f"\n  {label}")
        print(f"    {name} ({sec_name}, full string '{s_full}'):")

        # Show the next 4 strings after this one too (often related)
        cur = s_end + 1
        for n in range(1, 5):
            if cur >= len(sec_data):
                break
            # skip NUL padding
            while cur < len(sec_data) and sec_data[cur] == 0:
                cur += 1
            if cur >= len(sec_data):
                break
            next_end = sec_data.find(b"\x00", cur, cur + 256)
            if next_end == -1:
                next_end = cur + 64
            ns = sec_data[cur:next_end]
            ascii_only = all(32 <= b < 127 for b in ns) and len(ns) >= 3
            if ascii_only:
                ns_str = ns.decode("ascii", errors="replace")
                print(f"      next+{n} (VA=0x{sec_va + cur:016X}): {ns_str!r}")
            cur = next_end + 1


if __name__ == "__main__":
    main()
