"""Phase 6 save/load anchor scanner.

Scans WHGame.dll's .rdata for save/load-related anchor strings and prints
their VAs. Output feeds phase6_find_xref_functions.py which then walks
LEA xrefs back to function entries.

Methodology mirrors cap03_find_candidates.py: pefile to map sections,
linear byte search of .rdata for ASCII literals.

Usage:
    py phase6_dump_string_anchors.py <path/to/WHGame.dll>
"""

import sys
from pathlib import Path

import pefile


# Candidate anchor strings — known CryEngine / KCD2 save+load surface.
# Ordered: CryEngine string-table tokens first (highest signal because
# their xrefs are typically inside the function we want), then format
# strings / log messages second (xrefs may be inside log helpers, one
# call away from the function we want).
ANCHORS = [
    # CryEngine ISaveGame / ILoadGame infrastructure
    b"SaveGame",
    b"LoadGame",
    b"QuickSave",
    b"QuickLoad",
    b"AutoSave",
    b"autoSave",
    b"hardSave",
    b"HardSave",
    b"Hardsave",

    # CryAction SaveGame implementation strings — these show up in
    # CCryAction::SaveGame / LoadGame log output
    b"SaveGame in progress",
    b"Save in progress",
    b"Load in progress",
    b"Cannot save",
    b"Cannot load",
    b"Trying to save",
    b"Trying to load",
    b"Saving game to",
    b"Loading game from",
    b"Loading last saved game",     # known: appears in kcd.log per sol2 bisect
    b"saving game",
    b"loading game",

    # File extensions / file-path tokens
    b".sav",
    b".kcd2save",
    b".whs",
    b"savegame.xml",

    # CryEngine GameFramework listener names — only useful if we hook the
    # RegisterListener call rather than going through CompleteInit
    b"RegisterListener",
    b"OnSaveGame",
    b"OnLoadGame",
    b"OnSavegameFileLoadedInMemory",

    # FullSerialize / PostSerialize — IGame interface entry points
    b"FullSerialize",
    b"PostSerialize",

    # KCD2-specific console commands (from Warhorse modding wiki context)
    b"save_game",
    b"load_game",
    b"g_quickSave",
    b"g_quickLoad",
    b"sys_save",
    b"sys_load",
]


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: phase6_dump_string_anchors.py <WHGame.dll>")
    dll_path = Path(sys.argv[1])
    if not dll_path.exists():
        sys.exit(f"not found: {dll_path}")

    pe = pefile.PE(str(dll_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"Image: {dll_path}")
    print(f"ImageBase: 0x{image_base:016X}")
    print()

    sections = {}
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        sections[name] = (sec_va, sec_data)
        print(f"  {name:8s} VA=0x{sec_va:016X} size=0x{len(sec_data):X}")
    print()

    # Strings live in .rdata (and sometimes .data); xrefs come from .text.
    target_sections = [".rdata", ".data"]
    text_sec_va, text_sec_data = sections[".text"]
    print(f".text:  VA=0x{text_sec_va:016X} size=0x{len(text_sec_data):X}")
    print()

    # For each anchor: find all occurrences in target_sections that are
    # C-string-shaped (preceded by NUL or section start, followed by NUL).
    print("=" * 78)
    print("ANCHOR STRING HITS (C-string-shaped, preceded+followed by NUL)")
    print("=" * 78)

    found_anchors = {}  # anchor -> list of VAs
    for anchor in ANCHORS:
        hits = []
        for sec_name in target_sections:
            if sec_name not in sections:
                continue
            sec_va, sec_data = sections[sec_name]
            start = 0
            while True:
                idx = sec_data.find(anchor, start)
                if idx == -1:
                    break
                # Check C-string-shaped: byte before is NUL or section start,
                # byte after is NUL.
                preceding = sec_data[idx - 1] if idx > 0 else 0
                following = (
                    sec_data[idx + len(anchor)]
                    if idx + len(anchor) < len(sec_data)
                    else 0
                )
                if preceding == 0 and following == 0:
                    hits.append((sec_va + idx, sec_name))
                start = idx + 1
        if hits:
            found_anchors[anchor.decode("ascii", errors="replace")] = hits
            print(f"\n  {anchor!r}")
            for va, sec_name in hits:
                print(f"    {sec_name}  VA=0x{va:016X}")
        # don't print empty results — they spam

    print()
    print("=" * 78)
    print(f"SUMMARY: {len(found_anchors)} anchor(s) with at least one hit")
    print("=" * 78)
    for name, hits in found_anchors.items():
        print(f"  {name:40s}  {len(hits)} hit(s)")

    # Also verify the muyuanjin gEnv anchor still works.
    print()
    print("=" * 78)
    print("gEnv anchor (muyuanjin/kcd2db v1.4+ chain)")
    print("=" * 78)
    gEnv_anchor = b"exec autoexec.cfg"
    for sec_name in target_sections:
        if sec_name not in sections:
            continue
        sec_va, sec_data = sections[sec_name]
        idx = sec_data.find(gEnv_anchor)
        while idx != -1:
            preceding = sec_data[idx - 1] if idx > 0 else 0
            following = (
                sec_data[idx + len(gEnv_anchor)]
                if idx + len(gEnv_anchor) < len(sec_data)
                else 0
            )
            if preceding == 0 and following == 0:
                print(f"  {sec_name}  VA=0x{sec_va + idx:016X}  "
                      f"'{gEnv_anchor.decode('ascii')}'")
            idx = sec_data.find(gEnv_anchor, idx + 1)

    # Loose-fit substring scan — useful when we want a log-message
    # anchor that doesn't have to be a standalone C string. Hits get
    # printed with a 64-byte context window. We don't expect these to
    # be xref-anchor candidates directly (the xref site lands inside
    # printf-style helpers), but they help identify which functions
    # produce save/load log output for orientation.
    print()
    print("=" * 78)
    print("LOOSE-FIT SUBSTRING SCAN (no NUL boundary required)")
    print("=" * 78)
    loose_anchors = [
        b"savegame",
        b"savefile",
        b"loadgame",
        b"saveload",
        b"saveSlot",
        b"slotSave",
        b".sav",
        b"ISaveGame",
        b"ILoadGame",
        b"SerializeGame",
        b"FullSerialize",
        b"SaveGameSerializer",
        b"Cannot save",
        b"Cannot load",
        b"Loading last",
        b"Loading saved",
        b"Loading game",
        b"Saving game",
        b"save_game",
        b"load_game",
    ]
    for anchor in loose_anchors:
        hits = []
        for sec_name in target_sections:
            if sec_name not in sections:
                continue
            sec_va, sec_data = sections[sec_name]
            start = 0
            while True:
                idx = sec_data.find(anchor, start)
                if idx == -1:
                    break
                hits.append((sec_va + idx, sec_name, sec_data, idx))
                start = idx + 1
        if not hits:
            continue
        # Cap at 8 per anchor so output stays readable
        print(f"\n  {anchor!r}  ({len(hits)} hit(s))")
        for va, sec_name, sec_data, idx in hits[:8]:
            # 64-byte ASCII-printable context window
            ctx_start = max(0, idx - 4)
            ctx_end = min(len(sec_data), idx + len(anchor) + 60)
            ctx_bytes = sec_data[ctx_start:ctx_end]
            ctx_ascii = "".join(
                chr(b) if 32 <= b < 127 else "."
                for b in ctx_bytes
            )
            print(f"    {sec_name}  VA=0x{va:016X}  '{ctx_ascii}'")


if __name__ == "__main__":
    main()
