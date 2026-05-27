"""Call-graph walker for Phase 8 FIX A harvest.

Given a known anchor function's RVA inside WHGame.dll, disassemble its
body and enumerate every direct CALL (`E8 rel32`) target. Returns the
list of callee RVAs.

The walker stops at the first observed function-terminating instruction:
  - `C3` / `C2` (RET)
  - unconditional JMP outside the function range (heuristic — most
    intra-function JMPs are short; long JMPs to other RVAs are likely
    tail calls)

For tail calls (`E9 rel32`), the target is reported as a callee too
(tail-call optimization is a real call site).

Usage:
    py callgraph_walk.py --rva 0x71A5A4
    py callgraph_walk.py --rva 0x71A5A4 --max-size 256

This module is also importable: walk(rva, dll_path) -> list[Callee].
"""
from __future__ import annotations
import argparse
import struct
import sys
from dataclasses import dataclass

import capstone

# WHGame.dll calibration (verified 2026-05-21, see README.md)
DEFAULT_DLL = r'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll'
TEXT_VADDR = 0x1000
TEXT_RADDR = 0x400
TEXT_RSIZE = 0x3a01000


@dataclass
class Callee:
    """One call site found inside the walked function."""
    site_rva:    int      # RVA of the CALL/JMP instruction itself
    target_rva:  int      # RVA of the call target
    kind:        str      # 'call' or 'tailjmp'
    mnemonic:    str      # disassembly text, for debug


def _load_text(dll_path: str) -> bytes:
    with open(dll_path, 'rb') as f:
        f.seek(TEXT_RADDR)
        return f.read(TEXT_RSIZE)


def walk(anchor_rva: int,
         dll_path: str = DEFAULT_DLL,
         max_size: int = 4096,
         verbose: bool = False) -> list[Callee]:
    """Walk a function starting at anchor_rva in WHGame.dll.

    Disassembles forward, collecting direct call sites. Stops at the
    first RET, or after max_size bytes (whichever comes first).
    """
    text = _load_text(dll_path)
    start_off = anchor_rva - TEXT_VADDR
    if start_off < 0 or start_off >= len(text):
        raise ValueError(f'RVA {anchor_rva:#x} outside .text')

    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    cs.detail = False
    # Disassemble up to max_size bytes from the anchor.
    body = text[start_off:start_off + max_size]

    callees: list[Callee] = []
    ended = False
    for ins in cs.disasm(body, anchor_rva):
        if verbose:
            print(f'  {ins.address:#010x}  {ins.mnemonic:6s} {ins.op_str}  ({ins.bytes.hex()})')

        # Direct CALL rel32:  E8 + 4 bytes
        if ins.mnemonic == 'call' and ins.bytes[0] == 0xE8 and len(ins.bytes) == 5:
            rel = struct.unpack('<i', ins.bytes[1:])[0]
            target = ins.address + 5 + rel
            callees.append(Callee(site_rva=ins.address, target_rva=target, kind='call',
                                  mnemonic=f'{ins.mnemonic} {ins.op_str}'))
            continue

        # Tail jump JMP rel32:  E9 + 4 bytes
        if ins.mnemonic == 'jmp' and ins.bytes[0] == 0xE9 and len(ins.bytes) == 5:
            rel = struct.unpack('<i', ins.bytes[1:])[0]
            target = ins.address + 5 + rel
            # Heuristic: if the target is well outside the current function's
            # plausible body (>2KB away from current site), treat as a tail
            # call. Local jumps within the function will stay near the start.
            distance = abs(target - anchor_rva)
            if distance > 4096:
                callees.append(Callee(site_rva=ins.address, target_rva=target, kind='tailjmp',
                                      mnemonic=f'{ins.mnemonic} {ins.op_str}'))
                # Tail jumps end the function
                ended = True
                break
            # else: local control flow, continue past it
            continue

        # RET ends the function
        if ins.mnemonic in ('ret', 'retq'):
            ended = True
            break

        # INT3 padding after a function commonly appears. If we hit a run
        # of CC bytes, the function is over.
        if ins.bytes == b'\xCC':
            # Peek ahead: if the next 3 bytes are also CC, we're done.
            off = ins.address - anchor_rva
            if off + 4 <= len(body) and body[off:off+4] == b'\xCC\xCC\xCC\xCC':
                ended = True
                break

    return callees


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rva', required=True, type=lambda x: int(x, 0))
    ap.add_argument('--dll', default=DEFAULT_DLL)
    ap.add_argument('--max-size', type=lambda x: int(x, 0), default=4096)
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    callees = walk(args.rva, dll_path=args.dll, max_size=args.max_size, verbose=args.verbose)
    print(f'\n{len(callees)} call site(s) from RVA {args.rva:#x}:')
    seen_targets = {}
    for c in callees:
        marker = ''
        if c.target_rva in seen_targets:
            marker = f'  (also called at +{c.site_rva - seen_targets[c.target_rva]:#x})'
        else:
            seen_targets[c.target_rva] = c.site_rva
        print(f'  site={c.site_rva:#010x}  target={c.target_rva:#010x}  kind={c.kind:7s}  {c.mnemonic}{marker}')

    # Group by target
    uniq = {}
    for c in callees:
        uniq.setdefault(c.target_rva, []).append(c)
    print(f'\n{len(uniq)} unique target(s):')
    for tgt, sites in sorted(uniq.items()):
        kinds = ','.join(sorted({s.kind for s in sites}))
        print(f'  {tgt:#010x}  hit {len(sites)}x  kind={kinds}')


if __name__ == '__main__':
    main()
