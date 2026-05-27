"""verify_seed_sigs.py — verify Address Library seed candidates.

For each candidate pattern: scan WHGame.dll's .text section and report:
- Number of matches
- First-match RVA
- VA (base + RVA)

Run:  py verify_seed_sigs.py [path/to/WHGame.dll]

If no path is supplied, defaults to the live game install.
"""

import sys
from pathlib import Path

import pefile

DEFAULT_DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

# (name, pattern_string, source_tag)
SIGS = [
    # --- kcdx engine-internal sigs (from src/hooks.cpp) ---
    ("lua-pcall",
     "48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8",
     "kcdx-engine"),
    ("cgame-update",
     "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 "
     "48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? "
     "44 0F 29 48 ? 44 0F 29 50 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1",
     "kcdx-engine"),

    # --- yobson1/kcd2lua sigs (luaL_loadfile not used by kcdx; still seed-worthy) ---
    ("luaL-loadfile",
     "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D AC 24 ? ? ? ? "
     "48 81 EC 40 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 79 10",
     "yobson1"),

    # --- mempatch + kcdx test-plugin outfit-swap / IsInCombat sigs ---
    # outfit-swap site (the canonical 16-byte AOB; patch lands at offset 13)
    ("outfit-swap-aob",
     "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
     "mempatch"),
    # outfit-swap context (longer, for disambiguation; same site, anchor extended)
    ("outfit-swap-context",
     "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
     "mempatch"),
    # IsInCombat entry — kcdx test plugins use this exact 30-byte pattern
    ("isincombat-entry",
     "48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02",
     "kcdx-test"),
    # IsInCombat post-call disambig variant (comp-03 uses this version)
    ("isincombat-prologue-30",
     "48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01",
     "kcdx-test"),

    # --- CAP-03 candidate #4 (CGame::Update direct callee — per-frame UI pump) ---
    ("cgame-update-callee-ui-pump",
     "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 80 B9 C1 05 00 00 00 48 8B D9",
     "kcdx-test"),

    # --- muyuanjin gEnv anchor: 'exec autoexec.cfg' string ---
    # NOTE: this is a STRING in .rdata, not code in .text. Handled specially below.

    # --- muyuanjin gEnv resolver — V1.4+ context (mov r10, [rdx+0x118]) followed by lea ---
    # The full context-bearing prologue for the V1.4+ pConsole-MOV site:
    # 4C 8B 92 18 01 00 00      mov r10, [rdx+0x118]    (7 bytes)
    # 48 8D 15 ? ? ? ?          lea rdx, [rip+exec_autoexec]   (7 bytes)
    # We do NOT match the bytes between because the comment-block layout differs
    # by ±0x17 between v1.4 layouts. Match just the canonical 7-byte context prefix
    # and let the consumer walk to the LEA.
    ("genv-resolver-context-v14",
     "4C 8B 92 18 01 00 00 48 8D 15 ? ? ? ?",
     "muyuanjin-v1.4+"),
]

# Strings to search separately (in .rdata or any non-text section)
STRING_ANCHORS = [
    ("anchor-exec-autoexec-cfg", "exec autoexec.cfg", "muyuanjin"),
]


def parse_pattern(sig):
    bs, mask = [], []
    for tok in sig.split():
        if tok in ("?", "??"):
            bs.append(0)
            mask.append(False)
        else:
            bs.append(int(tok, 16))
            mask.append(True)
    return bytes(bs), mask


def find_all(data, pat_bytes, pat_mask, limit=None):
    n = len(pat_bytes)
    hits = []
    i = 0
    dlen = len(data)
    while i <= dlen - n:
        ok = True
        for j in range(n):
            if pat_mask[j] and data[i + j] != pat_bytes[j]:
                ok = False
                break
        if ok:
            hits.append(i)
            if limit is not None and len(hits) >= limit:
                return hits
            i += 1
        else:
            i += 1
    return hits


def main():
    dll = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL)
    if not dll.exists():
        print(f"ERR: WHGame.dll not found at {dll}")
        sys.exit(1)

    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"# WHGame.dll  base={hex(image_base)}  size={dll.stat().st_size}")
    print()

    # Identify sections
    text_section = None
    sections = []
    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sections.append((name, s))
        if name == ".text":
            text_section = s
    if text_section is None:
        print("ERR: .text section not found")
        sys.exit(1)

    text_data = text_section.get_data()
    text_va_base = image_base + text_section.VirtualAddress
    text_rva_base = text_section.VirtualAddress
    text_size = len(text_data)

    print(f"# .text  rva={hex(text_rva_base)}  size={hex(text_size)}  "
          f"va_base={hex(text_va_base)}")
    print()

    print("# === code patterns (scanned in .text) ===")
    for name, sig, source in SIGS:
        pat_bytes, pat_mask = parse_pattern(sig)
        hits = find_all(text_data, pat_bytes, pat_mask, limit=5)
        n_bytes = len(pat_bytes)
        if not hits:
            print(f"name={name:35s}  source={source:20s}  bytes={n_bytes:3d}  "
                  f"hits=0  STATUS=MISS")
            continue
        first_rva = text_rva_base + hits[0]
        first_va = text_va_base + hits[0]
        status = "OK-unique" if len(hits) == 1 else f"AMBIG x{len(hits)}"
        print(f"name={name:35s}  source={source:20s}  bytes={n_bytes:3d}  "
              f"hits={len(hits):d}  rva={hex(first_rva)}  va={hex(first_va)}  "
              f"STATUS={status}")

    print()
    print("# === string anchors (scanned in all sections) ===")
    for name, s, source in STRING_ANCHORS:
        # Scan all sections
        found = []
        for sec_name, s_obj in sections:
            data = s_obj.get_data()
            hits = find_all(data, s.encode("ascii"),
                            [True] * len(s.encode("ascii")),
                            limit=5)
            for h in hits:
                rva = s_obj.VirtualAddress + h
                va = image_base + rva
                found.append((sec_name, rva, va))
        if not found:
            print(f"name={name:35s}  source={source:20s}  hits=0  STATUS=MISS")
        else:
            for sec_name, rva, va in found:
                print(f"name={name:35s}  source={source:20s}  "
                      f"sec={sec_name:10s}  rva={hex(rva)}  va={hex(va)}")


if __name__ == "__main__":
    main()
