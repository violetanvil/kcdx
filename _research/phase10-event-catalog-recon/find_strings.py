#!/usr/bin/env python3
"""Anchor-string probe for WHGame.dll (front-2 entity/world events).

Tier-5 read-only static probe: extract printable ASCII + UTF-16LE strings from
the binary and report which event-anchor vocabulary is PRESENT as a literal.
Theory-independent: it reports raw ground truth (which strings exist), not a
test of any hypothesis. Reusable for any future anchor-presence question.

Usage:  python find_strings.py <regex> [<regex> ...]
        python find_strings.py --control     # known-present anchors (sanity)
"""
import re
import sys

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
MINLEN = 5


def extract(path):
    data = open(path, "rb").read()
    out = []
    # ASCII runs
    cur = bytearray()
    for b in data:
        if 0x20 <= b < 0x7F:
            cur.append(b)
        else:
            if len(cur) >= MINLEN:
                out.append(cur.decode("ascii"))
            cur = bytearray()
    if len(cur) >= MINLEN:
        out.append(cur.decode("ascii"))
    # UTF-16LE runs (ascii char + 0x00)
    cur = bytearray()
    i = 0
    n = len(data)
    while i + 1 < n:
        b, h = data[i], data[i + 1]
        if 0x20 <= b < 0x7F and h == 0x00:
            cur.append(b)
            i += 2
        else:
            if len(cur) >= MINLEN:
                out.append(cur.decode("ascii"))
            cur = bytearray()
            i += 1
    if len(cur) >= MINLEN:
        out.append(cur.decode("ascii"))
    return out


def main():
    args = sys.argv[1:]
    if not args:
        print("need a regex (or --control)", file=sys.stderr)
        return 2
    if args == ["--control"]:
        pats = [r"exec autoexec\.cfg", r"LocalizedStringManager",
                r"console command", r"\.cpp"]
    else:
        pats = args
    strings = extract(DLL)
    print(f"# total extracted strings: {len(strings)}")
    seen = set()
    for p in pats:
        rx = re.compile(p, re.IGNORECASE)
        hits = sorted({s for s in strings if rx.search(s)})
        print(f"\n## /{p}/  -> {len(hits)} unique")
        for h in hits[:60]:
            if h not in seen:
                print("  " + h)
                seen.add(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
