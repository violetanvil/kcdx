"""Grep all printable C strings in WHGame.dll for a regex.

Walks .rdata + .data, extracts C strings >= 4 chars, matches the
user-supplied regex (case-insensitive), prints VA + content.

Usage:
    py phase6_grep_strings.py <WHGame.dll> <regex>
"""
import re
import sys
from pathlib import Path
import pefile


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: phase6_grep_strings.py <WHGame.dll> <regex>")
    dll = Path(sys.argv[1])
    pat = re.compile(sys.argv[2].encode("utf-8"), re.IGNORECASE)

    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        if name not in (".rdata", ".data"):
            continue
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        i = 0
        while i < len(sec_data):
            if sec_data[i] == 0:
                i += 1
                continue
            j = sec_data.find(b"\x00", i)
            if j == -1:
                break
            s = sec_data[i:j]
            if len(s) >= 4 and all(32 <= b < 127 for b in s):
                if pat.search(s):
                    va = sec_va + i
                    print(f"  {name:8s} 0x{va:016X}  {s.decode('ascii')!r}")
            i = j + 1


if __name__ == "__main__":
    main()
