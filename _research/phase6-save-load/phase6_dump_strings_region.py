"""Dump all C strings within a VA range — used to map a contiguous
literal-pool block in .rdata so we can see the full vocabulary of a
nearby translation unit.

Usage:
    py phase6_dump_strings_region.py <WHGame.dll> <start_va_hex> <end_va_hex>
"""
import sys
from pathlib import Path
import pefile


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: phase6_dump_strings_region.py "
                 "<WHGame.dll> <start_va_hex> <end_va_hex>")
    dll = Path(sys.argv[1])
    start_va = int(sys.argv[2], 16)
    end_va = int(sys.argv[3], 16)

    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        if sec_va > end_va or sec_va + len(sec_data) < start_va:
            continue
        # Walk the section in [start_va, end_va) and print every
        # printable C string >= 4 chars.
        i = max(0, start_va - sec_va)
        while i < min(len(sec_data), end_va - sec_va):
            if sec_data[i] == 0:
                i += 1
                continue
            j = sec_data.find(b"\x00", i, min(len(sec_data), end_va - sec_va))
            if j == -1:
                break
            s = sec_data[i:j]
            if len(s) >= 4 and all(32 <= b < 127 for b in s):
                va = sec_va + i
                try:
                    s_str = s.decode("ascii")
                except UnicodeDecodeError:
                    s_str = repr(s)
                print(f"  0x{va:016X}  {s_str!r}")
            i = j + 1


if __name__ == "__main__":
    main()
