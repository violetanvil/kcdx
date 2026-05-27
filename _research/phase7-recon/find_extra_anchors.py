"""find_extra_anchors.py — locate additional well-known anchor strings
in WHGame.dll for the Address Library seed.

Each anchor below is a string we have HIGH confidence appears exactly once
in CryEngine binaries by convention (system status messages, banner
prints, registered command names). We resolve the string's RVA in .rdata
so the Address Library can ship the string as a known anchor for
future locator-pipeline resolution.

Output rows: (anchor_name, string, section, rva, va, hit_count)
"""

import sys
from pathlib import Path

import pefile

DEFAULT_DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

ANCHORS = [
    # Universal CryEngine console boot anchor
    ("anchor-exec-autoexec-cfg", "exec autoexec.cfg"),

    # System.LogAlways — pak Lua's most-called diagnostic; the C
    # implementation is a recognized cross-reference anchor
    ("anchor-string-System.LogAlways", "System.LogAlways"),
    ("anchor-string-System.Log", "System.Log"),

    # CryAction console banner — registered command name
    ("anchor-string-quit", "quit"),
    ("anchor-string-exit", "exit"),

    # Lua VM banner
    ("anchor-string-LUA_VERSION", "Lua 5.1"),

    # Script binding registration
    ("anchor-string-RegisterFunction", "RegisterFunction"),

    # Save/load anchors (foreshadowing Phase 6)
    ("anchor-string-OnSaveGame", "OnSaveGame"),
    ("anchor-string-OnLoadGame", "OnLoadGame"),

    # gEnv / IGameFramework
    ("anchor-string-IGameFramework", "IGameFramework"),
    ("anchor-string-gEnv", "gEnv"),

    # Common boot log lines (CryEngine convention)
    ("anchor-string-init-system", "Initializing System..."),
    ("anchor-string-cryengine-version", "CryEngine"),
]


def find_string_in_section(data, sec_va, sec_rva, image_base, target):
    needle = target.encode("ascii")
    hits = []
    i = 0
    while True:
        i = data.find(needle, i)
        if i < 0:
            break
        # Require it to be NUL-terminated and either start-of-section or
        # preceded by NUL (so we don't catch mid-string substrings).
        if i + len(needle) < len(data) and data[i + len(needle)] == 0:
            preceded_by_nul_or_start = (i == 0 or data[i - 1] == 0)
            if preceded_by_nul_or_start:
                hits.append(sec_rva + i)
        i += 1
    return hits


def main():
    dll = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL)
    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    sections = []
    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode()
        sections.append((name, s, s.get_data()))

    for anchor_name, target in ANCHORS:
        all_hits = []
        for sec_name, sec, data in sections:
            hits = find_string_in_section(
                data, sec.VirtualAddress + image_base,
                sec.VirtualAddress, image_base, target)
            for rva in hits:
                va = image_base + rva
                all_hits.append((sec_name, rva, va))
        if not all_hits:
            print(f"name={anchor_name:42s}  string={target!r:35s}  hits=0")
        elif len(all_hits) == 1:
            sec_name, rva, va = all_hits[0]
            print(f"name={anchor_name:42s}  string={target!r:35s}  "
                  f"sec={sec_name:8s}  rva={hex(rva)}  va={hex(va)}  UNIQUE")
        else:
            print(f"name={anchor_name:42s}  string={target!r:35s}  "
                  f"hits={len(all_hits)}  AMBIG")
            for sec_name, rva, va in all_hits[:3]:
                print(f"     [{sec_name}] rva={hex(rva)}  va={hex(va)}")


if __name__ == "__main__":
    main()
