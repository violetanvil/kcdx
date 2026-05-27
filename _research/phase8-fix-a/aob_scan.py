"""AOB uniqueness scanner for Phase 8 FIX A / §36 author-target work.

Closes the "unbuilt aob_scan.py" gap noted in README.md:101. Two jobs:

1. `scan(aob, dll_data)` — take an AOB string ("48 83 EC 28 ?? ..." with
   `??`/`?` wildcards), scan WHGame.dll's `.text` section, return EVERY
   match RVA. Used to FALSIFY uniqueness: a §36 entry-prologue AOB is only
   usable if exactly one `.text` match exists.

2. `read_entry(rva, dll_data, n)` + capstone disasm — read N entry bytes
   at a `.text` RVA (file offset = rva - section_rva + section_raw_ptr) and
   disassemble them, so you can locate the first variable/relocation byte
   (a rel32 call/jmp target after E8/E9, a RIP-relative disp32, an absolute
   imm) and cut the AOB slice just before it.

Opens WHGame.dll the same way string_xrefs.py does (raw file read +
hardcoded PE section calibration verified 2026-05-21). Default DLL is the
in-repo third-party-ghidra copy; override with --dll.

Usage:
    py aob_scan.py "48 83 EC 28 E8"            # scan a pattern, count matches
    py aob_scan.py "48 83 EC 28 E8" --rva 0xB9C1AC  # also disasm entry @ rva
    py aob_scan.py --disasm 0xD815A4 --n 24     # just disasm 24 bytes @ rva

Importable: scan(aob, dll_data) -> list[int] of RVAs; read_entry(...) ->
bytes; disasm_entry(...) -> list of (rva, mnemonic, op_str, raw_bytes).
"""
from __future__ import annotations
import argparse
import sys

try:
    import capstone
except ImportError:
    capstone = None

# WHGame.dll calibration — matches string_xrefs.py (verified 2026-05-21).
# In-repo copy is the default so the script is clone-and-go.
DLL = r'c:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll'
IMAGE_BASE = 0x180000000

SECTIONS = [
    # (name, vaddr, raddr, rsize)
    ('.text',   0x1000,    0x400,     0x3a01000),
    ('.rdata',  0x3a02000, 0x3a01400, 0xecf000),
    ('.data',   0x48d1000, 0x48d0400, 0x79ae00),
    ('.pdata',  0x5688000, 0x506b200, 0x357000),
    ('_RDATA',  0x59df000, 0x53c2200, 0x13400),
]

TEXT = SECTIONS[0]  # (name, vaddr, raddr, rsize)


def load_dll(path: str = DLL) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def rva_to_file_off(rva: int) -> int | None:
    for name, vaddr, raddr, rsize in SECTIONS:
        if vaddr <= rva < vaddr + rsize:
            return raddr + (rva - vaddr)
    return None


def parse_aob(aob: str) -> list[int | None]:
    """Parse "48 83 EC 28 ?? ..." into a list of ints, with None for any
    wildcard token (`?`, `??`, or `*`). Tolerant of extra whitespace and
    lowercase hex."""
    out: list[int | None] = []
    for tok in aob.split():
        if tok in ('?', '??', '*'):
            out.append(None)
        else:
            out.append(int(tok, 16))
    return out


def _match_at(buf: bytes, pos: int, pat: list[int | None]) -> bool:
    for k, p in enumerate(pat):
        if p is not None and buf[pos + k] != p:
            return False
    return True


def scan(aob: str, dll_data: bytes) -> list[int]:
    """Scan .text for the AOB. Return RVAs of every match (the RVA of the
    first byte of each match). The fixed (non-wildcard) bytes are the
    anchors; wildcard positions match any byte."""
    pat = parse_aob(aob)
    if not pat:
        return []
    _, vaddr, raddr, rsize = TEXT
    text = dll_data[raddr:raddr + rsize]
    n = len(pat)
    # Anchor the search on the first fixed byte to keep the scan fast over
    # the ~58 MB .text section, then verify the full pattern.
    first_fixed_idx = next((i for i, p in enumerate(pat) if p is not None), None)
    hits: list[int] = []
    limit = len(text) - n
    if first_fixed_idx is None:
        # All-wildcard pattern: degenerate, refuse (would match everywhere).
        return []
    anchor = pat[first_fixed_idx]
    i = 0
    while True:
        j = text.find(bytes([anchor]), i + first_fixed_idx)
        if j < 0:
            break
        start = j - first_fixed_idx
        if 0 <= start <= limit and _match_at(text, start, pat):
            hits.append(vaddr + start)
        i = start + 1
        if i > limit:
            break
    return hits


def read_entry(rva: int, dll_data: bytes, n: int = 24) -> bytes:
    """Read N bytes at a .text RVA. file offset = rva - vaddr + raddr."""
    off = rva_to_file_off(rva)
    if off is None:
        raise ValueError(f'RVA 0x{rva:x} not in any section')
    return dll_data[off:off + n]


def disasm_entry(rva: int, dll_data: bytes, n: int = 24):
    """Disassemble N entry bytes at a .text RVA. Returns a list of
    (insn_rva, mnemonic, op_str, raw_bytes) tuples. Requires capstone."""
    if capstone is None:
        raise RuntimeError('capstone not installed')
    raw = read_entry(rva, dll_data, n)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    out = []
    for insn in md.disasm(raw, rva):
        out.append((insn.address, insn.mnemonic, insn.op_str,
                    bytes(insn.bytes)))
    return out


def main():
    ap = argparse.ArgumentParser(description='AOB uniqueness scanner over WHGame.dll .text')
    ap.add_argument('aob', nargs='?', help='AOB string, e.g. "48 83 EC 28 E8" (?? = wildcard)')
    ap.add_argument('--dll', default=DLL)
    ap.add_argument('--rva', help='also disasm the function entry at this RVA (hex ok)')
    ap.add_argument('--disasm', help='ONLY disasm the entry at this RVA, no scan (hex ok)')
    ap.add_argument('--n', type=int, default=24, help='bytes to disasm (default 24)')
    args = ap.parse_args()

    dll_data = load_dll(args.dll)

    def _disasm(rva_str: str):
        rva = int(rva_str, 16) if rva_str.lower().startswith('0x') else int(rva_str, 16)
        print(f'\n=== entry disasm @ RVA 0x{rva:08x} (VA 0x{IMAGE_BASE + rva:x}) ===')
        for addr, mnem, ops, raw in disasm_entry(rva, dll_data, args.n):
            hexb = ' '.join(f'{b:02x}' for b in raw)
            print(f'  0x{addr:08x}: {hexb:<28} {mnem} {ops}')

    if args.disasm:
        _disasm(args.disasm)
        return

    if not args.aob:
        ap.error('provide an AOB to scan, or use --disasm RVA')

    hits = scan(args.aob, dll_data)
    print(f'AOB: {args.aob}')
    print(f'parsed: {len(parse_aob(args.aob))} tokens '
          f'({sum(1 for p in parse_aob(args.aob) if p is None)} wildcard)')
    print(f'.text matches: {len(hits)}')
    for h in hits:
        print(f'  RVA 0x{h:08x}  (VA 0x{IMAGE_BASE + h:x})')

    if args.rva:
        _disasm(args.rva)


if __name__ == '__main__':
    main()
