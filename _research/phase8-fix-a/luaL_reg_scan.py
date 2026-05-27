"""Scan WHGame.dll's .rdata for luaL_Reg[] arrays.

A luaL_Reg in Lua 5.1:
    struct luaL_Reg { const char *name; lua_CFunction func; };

In an MSVC-built x64 DLL, this is two 8-byte VAs (name into .rdata,
func into .text), terminated by {0, 0}. Default ImageBase is
0x180000000 so VAs look like 0x180_XXXXXXXX.

We scan .rdata for runs of >=2 such entries followed by a 16-byte
{0,0} terminator. Each candidate is then validated:
  - all name VAs land inside .rdata
  - all func VAs land inside .text
  - names are NUL-terminated ASCII identifiers
  - the function pointed to by each func VA looks like a function
    prologue (per .pdata)

Output: one block per identified luaL_Reg[], with (name, func RVA)
pairs ready to copy into lua_rvas.csv.

Usage:
    py luaL_reg_scan.py
    py luaL_reg_scan.py --min-size 4
"""
from __future__ import annotations
import argparse
import struct
import sys

DLL = r'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll'
IMAGE_BASE = 0x180000000

SECTIONS = [
    ('.text',   0x1000,    0x400,     0x3a01000),
    ('.rdata',  0x3a02000, 0x3a01400, 0xecf000),
    ('.data',   0x48d1000, 0x48d0400, 0x79ae00),
    ('.pdata',  0x5688000, 0x506b200, 0x357000),
    ('_RDATA',  0x59df000, 0x53c2200, 0x13400),
]


def va_in_section(va: int, section_name: str) -> bool:
    for name, vaddr, raddr, rsize in SECTIONS:
        if name == section_name:
            return vaddr <= (va - IMAGE_BASE) < (vaddr + rsize)
    return False


def rva_of_va(va: int) -> int | None:
    rva = va - IMAGE_BASE
    if rva < 0 or rva >> 32:
        return None
    return rva


def section_data(dll_data: bytes, name: str) -> tuple[int, int, bytes]:
    """Return (vaddr, raddr, bytes) for a section."""
    for sn, vaddr, raddr, rsize in SECTIONS:
        if sn == name:
            return vaddr, raddr, dll_data[raddr:raddr + rsize]
    raise KeyError(name)


def read_va_at_rva(dll_data: bytes, rva: int, size: int) -> bytes | None:
    """Read `size` bytes from the file at the given RVA. None if RVA falls outside
    any section."""
    for name, vaddr, raddr, rsize in SECTIONS:
        if vaddr <= rva < vaddr + rsize:
            off = raddr + (rva - vaddr)
            return dll_data[off:off + size]
    return None


def read_cstr_at_rva(dll_data: bytes, rva: int, max_len: int = 64) -> str | None:
    bs = read_va_at_rva(dll_data, rva, max_len)
    if bs is None:
        return None
    end = bs.find(b'\x00')
    if end < 0:
        return None
    try:
        s = bs[:end].decode('ascii')
    except UnicodeDecodeError:
        return None
    if not s:
        return None
    # luaL_Reg names are valid Lua identifiers: alnum + _
    for c in s:
        if not (c.isalnum() or c == '_'):
            return None
    return s


def looks_like_function(dll_data: bytes, rva: int, pdata_idx: list[tuple[int, int]]) -> bool:
    """Check whether rva is a function start (per .pdata)."""
    lo, hi = 0, len(pdata_idx)
    while lo < hi:
        mid = (lo + hi) // 2
        b, e = pdata_idx[mid]
        if b == rva:
            return True
        if b < rva:
            lo = mid + 1
        else:
            hi = mid
    return False


def build_pdata_index(dll_data: bytes) -> list[tuple[int, int]]:
    _, pdata_raddr, pdata_bytes = section_data(dll_data, '.pdata')
    entries = []
    for i in range(0, len(pdata_bytes) - 12, 12):
        begin, end, _u = struct.unpack_from('<III', pdata_bytes, i)
        if begin == 0 and end == 0:
            break
        entries.append((begin, end))
    entries.sort()
    return entries


def scan(dll_data: bytes, min_size: int = 2) -> list[list[tuple[str, int]]]:
    rdata_vaddr, rdata_raddr, rdata = section_data(dll_data, '.rdata')
    text_vaddr, _, _ = section_data(dll_data, '.text')
    text_size = SECTIONS[0][3]
    rdata_size = len(rdata)
    pdata_idx = build_pdata_index(dll_data)

    # We scan at 8-byte alignment (qword) — luaL_Reg arrays are qword-aligned.
    found_blocks: list[list[tuple[str, int]]] = []
    i = 0
    # Re-read .rdata bytes
    while i < rdata_size - 16:
        # Try to interpret 16 bytes here as (name_va, func_va)
        name_va, func_va = struct.unpack_from('<QQ', rdata, i)
        if name_va == 0 or func_va == 0:
            i += 8
            continue
        # Both must be 0x180_XXXXXXXX (default ImageBase)
        if (name_va >> 32) != 1 or (func_va >> 32) != 1:
            i += 8
            continue
        name_rva = rva_of_va(name_va)
        func_rva = rva_of_va(func_va)
        if name_rva is None or func_rva is None:
            i += 8
            continue
        # name must point into .rdata
        if not (rdata_vaddr <= name_rva < rdata_vaddr + rdata_size):
            i += 8
            continue
        # func must point into .text
        if not (text_vaddr <= func_rva < text_vaddr + text_size):
            i += 8
            continue
        # Check name is a valid identifier
        name = read_cstr_at_rva(dll_data, name_rva)
        if not name:
            i += 8
            continue
        # Check func is a real function per .pdata
        if not looks_like_function(dll_data, func_rva, pdata_idx):
            i += 8
            continue

        # Looks valid. Walk forward gathering entries until we hit {0,0}.
        block_start = i
        entries: list[tuple[str, int]] = []
        while i < rdata_size - 16:
            nva, fva = struct.unpack_from('<QQ', rdata, i)
            if nva == 0 and fva == 0:
                # Terminator
                i += 16
                break
            nrva = rva_of_va(nva) if nva else None
            frva = rva_of_va(fva) if fva else None
            if nrva is None or frva is None:
                break
            if (nva >> 32) != 1 or (fva >> 32) != 1:
                break
            if not (rdata_vaddr <= nrva < rdata_vaddr + rdata_size):
                break
            if not (text_vaddr <= frva < text_vaddr + text_size):
                break
            nm = read_cstr_at_rva(dll_data, nrva)
            if not nm:
                break
            if not looks_like_function(dll_data, frva, pdata_idx):
                break
            entries.append((nm, frva))
            i += 16

        if len(entries) >= min_size:
            # Record the block and continue past the terminator
            block_rva = rdata_vaddr + block_start
            found_blocks.append([(block_rva, len(entries))] + entries)  # first elem is metadata
        # i is already advanced past the terminator (or past where we gave up)
        # Skip the lookahead bytes we consumed
        if i == block_start:
            i += 8

    return found_blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dll', default=DLL)
    ap.add_argument('--min-size', type=int, default=3,
                    help='minimum entries per array to report (default 3)')
    args = ap.parse_args()

    with open(args.dll, 'rb') as f:
        dll_data = f.read()

    blocks = scan(dll_data, min_size=args.min_size)
    print(f'Found {len(blocks)} luaL_Reg[]-like arrays (min_size={args.min_size})')
    for blk in blocks:
        meta = blk[0]  # (block_rva, n_entries)
        entries = blk[1:]
        block_rva, nent = meta
        names_preview = ', '.join(e[0] for e in entries[:5])
        if len(entries) > 5:
            names_preview += f', ... ({len(entries)} total)'
        print(f'\n=== block @ RVA 0x{block_rva:x}  ({nent} entries): {names_preview}')
        for name, frva in entries:
            print(f'    {name:30s}  RVA 0x{frva:08x}')


if __name__ == '__main__':
    main()
