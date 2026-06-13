#!/usr/bin/env python3
"""Extract ASCII strings from WHGame.dll and bucket the ones relevant to
the script-driven event question (front 3). Ground-truth observation first,
theory-independent: dump what Lua/event/script string literals actually exist.

Usage:
  python extract_strings.py <regex> [minlen]   # filtered, case-insensitive
  python extract_strings.py --all-lua          # known Lua/script anchor set
Outputs matched strings, one per line, deduped+sorted, with a count header.
"""
import sys, re, pefile

DLL = "third-party-ghidra/WHGame.dll"
MINLEN = 5

def all_strings(minlen):
    pe = pefile.PE(DLL, fast_load=True)
    out = []
    cur = bytearray()
    for sec in pe.sections:
        data = sec.get_data()
        for b in data:
            if 32 <= b < 127:
                cur.append(b)
            else:
                if len(cur) >= minlen:
                    out.append(cur.decode("ascii", "replace"))
                cur = bytearray()
        if len(cur) >= minlen:
            out.append(cur.decode("ascii", "replace"))
        cur = bytearray()
    return out

def main():
    if len(sys.argv) < 2:
        print("usage: extract_strings.py <regex>|--all-lua [minlen]"); return
    arg = sys.argv[1]
    minlen = int(sys.argv[2]) if len(sys.argv) > 2 else MINLEN
    strings = all_strings(minlen)
    if arg == "--all-lua":
        pat = re.compile(
            r"(\.lua|ScriptBind|RegisterFunction|gEnv|pScriptSystem|"
            r"OnQuest|OnPerk|OnLevel|OnSkill|OnStat|OnDialog|Subtitle|"
            r"Player\.On|Game\.On|RPG|Soul|Perk|QuestSystem|DialogSystem)",
            re.I)
    else:
        pat = re.compile(arg, re.I)
    matched = sorted(set(s for s in strings if pat.search(s)))
    print(f"# total strings(minlen={minlen}): {len(strings)}")
    print(f"# matched /{arg}/: {len(matched)}")
    for s in matched:
        print(s)

if __name__ == "__main__":
    main()
