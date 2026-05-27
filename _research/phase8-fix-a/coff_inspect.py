"""COFF .obj parser for Phase 8 FIX A harvest.

Extracts function definitions from a COFF object file produced by MSVC.
For each function we get:
  - name (e.g. "lua_pcall")
  - bytes (the function body in .text$mn or similar)
  - relocations (list of (offset_in_func, type, target_symbol))

This is enough for the relocation-aware AOB scanner (aob_scan.py) and
for matching call sites inside one function to other functions in the
same .obj (helps the call-graph walker disambiguate).

Usage:
    py coff_inspect.py path/to/lapi.obj
    py coff_inspect.py path/to/lapi.obj --symbol lua_pcall
    py coff_inspect.py path/to/lapi.obj --symbol lua_pcall --dump

CLI prints all functions found by default (one per line); --symbol
filters; --dump prints bytes + relocations for the selected symbol.

This module is also importable: ParseObj(path) -> Obj, with Obj.functions
keyed by symbol name. See bottom of file.
"""
from __future__ import annotations
import struct
import sys
from dataclasses import dataclass, field

# COFF constants we actually use
IMAGE_FILE_MACHINE_AMD64 = 0x8664

IMAGE_SCN_CNT_CODE            = 0x00000020
IMAGE_SCN_LNK_COMDAT          = 0x00001000

# AMD64 relocation types we care about (winnt.h IMAGE_REL_AMD64_*)
REL_ABSOLUTE = 0x0000
REL_ADDR64   = 0x0001  # 64-bit VA
REL_ADDR32   = 0x0002  # 32-bit VA
REL_ADDR32NB = 0x0003  # 32-bit VA without image base
REL_REL32    = 0x0004  # 32-bit RIP-relative (calls, RIP-relative loads)
REL_REL32_1  = 0x0005  # rel32 minus 1 byte
REL_REL32_2  = 0x0006
REL_REL32_3  = 0x0007
REL_REL32_4  = 0x0008
REL_REL32_5  = 0x0009
REL_SECTION  = 0x000A
REL_SECREL   = 0x000B
REL_SECREL7  = 0x000C
REL_TOKEN    = 0x000D
REL_SREL32   = 0x000E
REL_PAIR     = 0x000F
REL_SSPAN32  = 0x0010

# Storage class
IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC   = 3
IMAGE_SYM_CLASS_LABEL    = 6
IMAGE_SYM_CLASS_FUNCTION = 0x65  # 101


@dataclass
class Reloc:
    """One relocation inside a function body."""
    offset:        int   # byte offset from start of function
    type:          int   # REL_* constant
    target_symbol: str   # symbol the relocation points to
    addend:        int   # value already in the bytes at offset (sign-extended for rel32)


@dataclass
class Function:
    """A function defined in this .obj.

    `bytes_` covers offset 0 through size-1 from the start of the function
    in its source section (typically .text$mn). Relocations are listed in
    source-file order (= offset order, MSVC always sorts them).
    """
    name:      str
    section:   str
    section_offset: int  # byte offset within the section's body where the function starts
    size:      int        # function size in bytes (may overshoot the real end; see notes)
    bytes_:    bytes
    relocs:    list[Reloc] = field(default_factory=list)


@dataclass
class Obj:
    path:      str
    machine:   int
    functions: dict[str, Function] = field(default_factory=dict)


def _read_str_at(strtab: bytes, off: int) -> str:
    """Read NUL-terminated string from string table at offset off."""
    end = strtab.find(b'\x00', off)
    if end < 0:
        end = len(strtab)
    return strtab[off:end].decode('utf-8', errors='replace')


def _decode_symbol_name(raw_name: bytes, strtab: bytes) -> str:
    """COFF symbol-name encoding: if first 4 bytes are zero, the next 4
    bytes are an offset into the string table; otherwise the 8-byte
    field is the name itself (NUL-padded, NOT NUL-terminated if it
    fills the whole field)."""
    if raw_name[:4] == b'\x00\x00\x00\x00':
        off = struct.unpack('<I', raw_name[4:8])[0]
        return _read_str_at(strtab, off)
    end = raw_name.find(b'\x00')
    if end < 0:
        end = 8
    return raw_name[:end].decode('utf-8', errors='replace')


def parse_obj(path: str) -> Obj:
    with open(path, 'rb') as f:
        data = f.read()

    # COFF file header (20 bytes)
    machine, nsec, ts, ptr_symtab, n_symbols, opt_hdr_sz, characteristics = \
        struct.unpack_from('<HHIIIHH', data, 0)

    if machine != IMAGE_FILE_MACHINE_AMD64:
        raise ValueError(f'{path}: not AMD64 ({machine:#x})')

    # String table sits right after the symbol table. Each symbol is 18 bytes.
    symtab_off = ptr_symtab
    symtab_end = symtab_off + n_symbols * 18
    strtab_size = struct.unpack_from('<I', data, symtab_end)[0]
    strtab = data[symtab_end:symtab_end + strtab_size]

    # Read section headers
    sec_hdr_off = 20 + opt_hdr_sz
    sections = []
    for i in range(nsec):
        off = sec_hdr_off + i * 40
        name_raw = data[off:off+8]
        if name_raw[:1] == b'/':
            # "/NNN" form: name is in string table
            num = int(name_raw[1:].rstrip(b'\x00').decode())
            sec_name = _read_str_at(strtab, num)
        else:
            end = name_raw.find(b'\x00')
            if end < 0: end = 8
            sec_name = name_raw[:end].decode()
        vsize, vaddr, raw_size, raw_ptr, reloc_ptr, lineno_ptr, nrelocs, nlinenos, sec_chars = \
            struct.unpack_from('<IIIIIIHHI', data, off + 8)
        sections.append({
            'index': i + 1,  # COFF section indices are 1-based
            'name': sec_name,
            'raw_size': raw_size,
            'raw_ptr': raw_ptr,
            'reloc_ptr': reloc_ptr,
            'nrelocs': nrelocs,
            'chars': sec_chars,
        })

    # Read all symbols. Skip aux records (the n_aux follow each symbol).
    symbols = []  # (index, name, value, sec_idx, type, storage_class, n_aux)
    i = 0
    while i < n_symbols:
        off = symtab_off + i * 18
        name_raw = data[off:off+8]
        value, sec_idx, sym_type, storage_class, n_aux = \
            struct.unpack_from('<IhHBB', data, off + 8)
        name = _decode_symbol_name(name_raw, strtab)
        symbols.append({
            'index': i,
            'name': name,
            'value': value,
            'sec_idx': sec_idx,
            'type': sym_type,
            'storage_class': storage_class,
            'n_aux': n_aux,
        })
        i += 1 + n_aux

    # Symbol lookup by index (auxiliary records share the same indexing space)
    sym_by_index = {s['index']: s for s in symbols}

    # Find function-defining symbols. MSVC emits external/static symbols
    # pointing into a code section; the function body starts at
    # section.raw_ptr + value. The size of the function is the distance to
    # the next symbol in the same section (or end of section).
    func_candidates = []
    for s in symbols:
        if s['sec_idx'] <= 0:
            continue
        sec = sections[s['sec_idx'] - 1]
        if not (sec['chars'] & IMAGE_SCN_CNT_CODE):
            continue
        if s['storage_class'] not in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC):
            continue
        # type field: low 4 bits = base type; high 4 bits = derived type. For
        # functions the derived type is 2 (function). Some symbols are also
        # labels — those have storage class LABEL we excluded above.
        # We accept any symbol in a code section here; non-function labels
        # are pruned later by checking that the name doesn't look like a
        # local jump label.
        func_candidates.append(s)

    # Group by section, sort by value, compute sizes from gaps.
    by_section = {}
    for s in func_candidates:
        by_section.setdefault(s['sec_idx'], []).append(s)
    for sec_idx, syms in by_section.items():
        syms.sort(key=lambda x: x['value'])

    obj = Obj(path=path, machine=machine)
    for sec_idx, syms in by_section.items():
        sec = sections[sec_idx - 1]
        # Build relocation list for this section, indexed by section-offset.
        relocs_by_secoff = []
        for k in range(sec['nrelocs']):
            roff = sec['reloc_ptr'] + k * 10
            va, sym_idx, rtype = struct.unpack_from('<IIH', data, roff)
            rel_target_name = sym_by_index.get(sym_idx, {}).get('name', f'?sym{sym_idx}')
            relocs_by_secoff.append((va, rtype, rel_target_name))

        for j, s in enumerate(syms):
            # End of this function = start of next symbol's value, or end of section
            if j + 1 < len(syms):
                end = syms[j+1]['value']
            else:
                end = sec['raw_size']
            size = end - s['value']
            if size <= 0:
                continue
            body_start = sec['raw_ptr'] + s['value']
            body = data[body_start:body_start + size]

            # Filter relocs that fall inside this function
            fn_relocs = []
            for va, rtype, tgt in relocs_by_secoff:
                if s['value'] <= va < end:
                    addend = 0
                    rel_off = va - s['value']
                    if rtype in (REL_REL32, REL_REL32_1, REL_REL32_2, REL_REL32_3, REL_REL32_4, REL_REL32_5):
                        if rel_off + 4 <= size:
                            addend = struct.unpack_from('<i', body, rel_off)[0]
                    elif rtype == REL_ADDR32 or rtype == REL_ADDR32NB:
                        if rel_off + 4 <= size:
                            addend = struct.unpack_from('<I', body, rel_off)[0]
                    elif rtype == REL_ADDR64:
                        if rel_off + 8 <= size:
                            addend = struct.unpack_from('<Q', body, rel_off)[0]
                    fn_relocs.append(Reloc(offset=rel_off, type=rtype, target_symbol=tgt, addend=addend))

            # Filter out symbols that are clearly local jump labels (no name or $-prefixed).
            # MSVC strips local labels by default; what makes it into the symbol table is
            # public + static functions. So we keep everything that survived above.
            obj.functions[s['name']] = Function(
                name=s['name'],
                section=sec['name'],
                section_offset=s['value'],
                size=size,
                bytes_=body,
                relocs=fn_relocs,
            )

    return obj


def reloc_mask(fn: Function) -> bytes:
    """Build a byte-mask matching fn.bytes_ length. 0x00 = "match this byte
    exactly"; 0xFF = "ignore (linker patches this)". The mask widths for
    each relocation type follow the AMD64 reloc spec.
    """
    mask = bytearray(len(fn.bytes_))
    for r in fn.relocs:
        if r.type in (REL_REL32, REL_REL32_1, REL_REL32_2, REL_REL32_3, REL_REL32_4, REL_REL32_5):
            width = 4
        elif r.type in (REL_ADDR32, REL_ADDR32NB, REL_SECREL, REL_SREL32, REL_SSPAN32):
            width = 4
        elif r.type == REL_ADDR64:
            width = 8
        elif r.type == REL_SECTION:
            width = 2
        elif r.type == REL_SECREL7:
            width = 1
        elif r.type == REL_PAIR or r.type == REL_TOKEN or r.type == REL_ABSOLUTE:
            width = 0  # don't mask
        else:
            width = 4  # conservative default
        for b in range(width):
            if r.offset + b < len(mask):
                mask[r.offset + b] = 0xFF
    return bytes(mask)


def fmt_reloc_type(t: int) -> str:
    return {
        REL_ABSOLUTE: 'ABS',
        REL_ADDR64: 'ADDR64',
        REL_ADDR32: 'ADDR32',
        REL_ADDR32NB: 'ADDR32NB',
        REL_REL32: 'REL32',
        REL_REL32_1: 'REL32_1',
        REL_REL32_2: 'REL32_2',
        REL_REL32_3: 'REL32_3',
        REL_REL32_4: 'REL32_4',
        REL_REL32_5: 'REL32_5',
        REL_SECTION: 'SECTION',
        REL_SECREL: 'SECREL',
        REL_SECREL7: 'SECREL7',
        REL_TOKEN: 'TOKEN',
        REL_SREL32: 'SREL32',
        REL_PAIR: 'PAIR',
        REL_SSPAN32: 'SSPAN32',
    }.get(t, f'?{t:#x}')


def main(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--symbol', help='show only this symbol')
    ap.add_argument('--dump', action='store_true', help='dump bytes + relocs')
    ap.add_argument('--limit', type=int, default=0, help='cap bytes dumped (default: full)')
    args = ap.parse_args(argv)

    obj = parse_obj(args.path)
    if not args.symbol:
        for name, fn in sorted(obj.functions.items()):
            print(f'{name:40s}  size={fn.size:6d}  relocs={len(fn.relocs):3d}  section={fn.section}')
        print(f'\n{len(obj.functions)} functions total')
        return

    fn = obj.functions.get(args.symbol)
    if not fn:
        print(f'symbol not found: {args.symbol}', file=sys.stderr)
        sys.exit(1)

    print(f'{fn.name}  section={fn.section}  size={fn.size}  relocs={len(fn.relocs)}')

    if not args.dump:
        return

    # Print bytes with reloc positions annotated
    body = fn.bytes_
    mask = reloc_mask(fn)
    limit = args.limit if args.limit > 0 else len(body)
    width = 16
    for i in range(0, min(limit, len(body)), width):
        chunk = body[i:i+width]
        m_chunk = mask[i:i+width]
        hex_bytes = ' '.join(
            '??' if m else f'{b:02X}'
            for b, m in zip(chunk, m_chunk)
        )
        print(f'  +{i:04x}  {hex_bytes}')

    print('  relocs:')
    for r in fn.relocs:
        print(f'    +{r.offset:04x}  {fmt_reloc_type(r.type):8s}  addend={r.addend:#x}  -> {r.target_symbol}')


if __name__ == '__main__':
    main(sys.argv[1:])
