"""String-xref scanner for Phase 8 FIX A harvest.

Find code references to a string in WHGame.dll.

Workflow:
1. Locate the string in .rdata (literal bytes search).
2. Scan .text for any rel32 reference whose decoded target lands on the
   string's RVA. The common form is `LEA reg, [rip+rel32]` (7 bytes with
   REX prefix). Also matches `MOV reg, [rip+rel32]`, etc.
3. For each xref site, walk backward to find the enclosing function
   start. Heuristic: scan backward for the nearest `0xCC` run (interrupt
   pads between functions) followed by a standard prologue
   (`48 89 5C 24 ?` or `48 8B C4` or `40 5* push`).

Usage:
    py string_xrefs.py "unable to dump given function"
    py string_xrefs.py "luaopen_table"  # finds the named open routine

This module is also importable: find_xrefs(string) -> list[Xref].
"""
from __future__ import annotations
import argparse
import struct
import sys
from dataclasses import dataclass

# WHGame.dll calibration
DLL = r'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll'

SECTIONS = [
    # (name, vaddr, raddr, rsize)
    ('.text',   0x1000,    0x400,     0x3a01000),
    ('.rdata',  0x3a02000, 0x3a01400, 0xecf000),
    ('.data',   0x48d1000, 0x48d0400, 0x79ae00),
    ('.pdata',  0x5688000, 0x506b200, 0x357000),
    ('_RDATA',  0x59df000, 0x53c2200, 0x13400),
]


@dataclass
class Xref:
    """One code reference to the target VA."""
    site_rva:     int       # RVA of the rel32 operand bytes (the 4 bytes themselves)
    instr_rva:    int       # RVA of the instruction start (best-guess; site_rva - usual_offset)
    rip_after:    int       # the RIP value the CPU sees at this rel32 (= rva of next instruction)
    target_rva:   int
    enclosing_fn: int | None  # RVA of nearest plausible function start, or None


def file_off_to_rva(off: int) -> int | None:
    for name, vaddr, raddr, rsize in SECTIONS:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr)
    return None


def rva_to_file_off(rva: int) -> int | None:
    for name, vaddr, raddr, rsize in SECTIONS:
        # virtual size may differ from raw size, but for our scanning purposes raw is what we read
        if vaddr <= rva < vaddr + rsize:
            return raddr + (rva - vaddr)
    return None


def find_string(dll_data: bytes, s: bytes) -> list[int]:
    """Return file offsets of literal byte matches for s (with trailing NUL).
    Most strings in .rdata are NUL-terminated; require the NUL to avoid
    spurious substring hits."""
    needle = s + b'\x00'
    hits = []
    i = 0
    while True:
        j = dll_data.find(needle, i)
        if j < 0: break
        hits.append(j)
        i = j + 1
    return hits


def find_rel32_xrefs(dll_data: bytes, target_rva: int) -> list[Xref]:
    """Scan .text for any rel32 whose decoded address == target_rva.

    Brute force: at every byte position in .text, interpret the next 4 bytes
    as an i32 and check if (rva_at_pos + 4) + rel == target_rva. To avoid
    insane numbers of false positives, only report if the byte immediately
    BEFORE the rel32 looks like a valid x64 instruction byte that takes a
    rel32 operand. The common forms are:

        LEA reg, [rip+rel32]:   48/4C 8D ?? rel32     ← most common for strings
        MOV reg, [rip+rel32]:   48/4C 8B ?? rel32
        CALL [rip+rel32]:       FF 15 rel32
        JMP  [rip+rel32]:       FF 25 rel32
        CALL rel32:             E8 rel32
        JMP  rel32:             E9 rel32

    For string references we care most about LEA (loads address of literal).
    """
    text_name, text_vaddr, text_raddr, text_rsize = SECTIONS[0]
    text = dll_data[text_raddr:text_raddr + text_rsize]

    xrefs = []
    # Scan every byte. For each candidate rel32 at offset i, the instruction
    # ends at i+4 and that's where RIP points after this instruction. So
    # rip_after_va = text_vaddr + i + 4. target = rip_after_va + signed(rel32).
    for i in range(0, len(text) - 4):
        rel = struct.unpack_from('<i', text, i)[0]
        rip_after_va = text_vaddr + i + 4
        target = rip_after_va + rel
        if target != target_rva:
            continue

        # Look at the preceding 1-3 bytes for a likely opcode pattern.
        # We accept LEA/MOV reg, [rip+rel32] forms with 0-3 prefix bytes,
        # and the absolute call/jmp through [rip+rel32] forms.
        prefix = text[max(0, i-7):i]
        if len(prefix) < 3:
            continue

        # Common form: <REX> 8D ?? rel32  (LEA r64, [rip+rel32])
        #   REX = 0x40..0x4F, then 8D, then ModR/M byte with mod=00 rm=101
        #   The ModR/M byte has form 00 reg 101 = 0x05 | (reg<<3) = 0x05/0x0D/0x15/...
        # Common form: <REX> 8B ?? rel32  (MOV r64, [rip+rel32])
        # We accept any case where the byte at i-2 is 0x8D or 0x8B and the
        # byte at i-3 has the high nibble 0x4 (REX). Without REX it's the 32-bit form.
        opcode_byte = prefix[-2]
        modrm_byte = prefix[-1]
        # ModR/M must have mod=00 and rm=101 for [rip+disp32]
        modrm_ok = (modrm_byte & 0xC7) == 0x05

        is_lea_or_mov = opcode_byte in (0x8D, 0x8B)
        is_rel32_call = opcode_byte == 0xE8 and len(prefix) >= 1  # call rel32 — pattern is E8 rel32 (no ModR/M)
        is_rel32_jmp  = opcode_byte == 0xE9
        # For E8/E9 the "modrm" position is actually part of the immediate, so we don't gate on modrm.

        if is_lea_or_mov and modrm_ok:
            # estimate instruction start: REX (1 byte) + opcode (1 byte) + modrm (1 byte) + rel32
            # = 4 bytes total without REX, 4 with REX. REX is at i-3 if present.
            rex_byte = prefix[-3] if len(prefix) >= 3 else 0
            has_rex = 0x40 <= rex_byte <= 0x4F
            instr_start = i - (3 if has_rex else 2)
            xrefs.append(Xref(
                site_rva=text_vaddr + i,
                instr_rva=text_vaddr + instr_start,
                rip_after=rip_after_va,
                target_rva=target,
                enclosing_fn=None,
            ))
            continue

        # Absolute call/jmp through memory: FF 15 rel32 or FF 25 rel32
        if opcode_byte == 0xFF and modrm_byte in (0x15, 0x25):
            instr_start = i - 2
            xrefs.append(Xref(
                site_rva=text_vaddr + i,
                instr_rva=text_vaddr + instr_start,
                rip_after=rip_after_va,
                target_rva=target,
                enclosing_fn=None,
            ))
            continue

        # Direct rel32 call/jmp (these point to *code* not strings, but include
        # for completeness when target is in .text).
        # i-1 should be E8 or E9.
        if i >= 1 and text[i-1] in (0xE8, 0xE9):
            instr_start = i - 1
            xrefs.append(Xref(
                site_rva=text_vaddr + i,
                instr_rva=text_vaddr + instr_start,
                rip_after=rip_after_va,
                target_rva=target,
                enclosing_fn=None,
            ))
            continue

    return xrefs


_PDATA_INDEX: list[tuple[int, int]] | None = None  # sorted (begin, end) RVA pairs


def _build_pdata_index(dll_data: bytes) -> list[tuple[int, int]]:
    """Parse .pdata (exception directory). Each RUNTIME_FUNCTION is 12 bytes:
    BeginAddress, EndAddress, UnwindInfoAddress. Returns sorted list of
    (begin, end) RVAs covering every function in the binary.

    This is the authoritative function-bound table — every function with a
    non-trivial prologue has an entry. Far more reliable than the CC-pad
    heuristic for finding function starts in PGO-laid-out code.
    """
    pdata_name = '.pdata'
    pdata_raddr = pdata_rsize = None
    for name, vaddr, raddr, rsize in SECTIONS:
        if name == pdata_name:
            pdata_raddr, pdata_rsize = raddr, rsize
            break
    if pdata_raddr is None:
        return []

    entries = []
    for i in range(0, pdata_rsize - 12, 12):
        begin, end, unwind = struct.unpack_from('<III', dll_data, pdata_raddr + i)
        if begin == 0 and end == 0:
            break
        entries.append((begin, end))
    entries.sort()
    return entries


def find_enclosing_function(dll_data: bytes, site_rva: int, max_scan: int = 0x4000) -> int | None:
    """Return the RVA of the function containing site_rva, using .pdata
    (exception directory) as the authoritative source.

    .pdata enumerates every function with a non-trivial prologue. Leaf
    functions without a prologue are missing from .pdata, but those are
    rare in code as large as Lua's API.
    """
    global _PDATA_INDEX
    if _PDATA_INDEX is None:
        _PDATA_INDEX = _build_pdata_index(dll_data)

    # Binary search for the entry containing site_rva
    lo, hi = 0, len(_PDATA_INDEX)
    while lo < hi:
        mid = (lo + hi) // 2
        b, e = _PDATA_INDEX[mid]
        if site_rva < b:
            hi = mid
        elif site_rva >= e:
            lo = mid + 1
        else:
            return b
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('string', help='string to search for (NUL-terminated match)')
    ap.add_argument('--dll', default=DLL)
    args = ap.parse_args()

    with open(args.dll, 'rb') as f:
        dll_data = f.read()

    needle = args.string.encode('utf-8')
    str_offs = find_string(dll_data, needle)
    if not str_offs:
        print(f'string not found: {args.string!r}')
        sys.exit(2)

    for off in str_offs:
        rva = file_off_to_rva(off)
        print(f'\n=== string at file=0x{off:x} RVA=0x{rva:x} VA=0x{0x180000000 + rva:x} ===')

        xrefs = find_rel32_xrefs(dll_data, rva)
        print(f'  {len(xrefs)} xref(s):')
        for x in xrefs:
            fn = find_enclosing_function(dll_data, x.instr_rva)
            fn_s = f'0x{fn:x}' if fn else '(not found)'
            print(f'    instr=0x{x.instr_rva:08x}  rel32_at=0x{x.site_rva:08x}  enclosing_fn={fn_s}')


if __name__ == '__main__':
    main()
