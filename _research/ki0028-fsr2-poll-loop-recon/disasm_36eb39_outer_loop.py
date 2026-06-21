"""KI-0028 outer-loop read (static, no launch) — the ONE unread load-bearing fact.

The wedge stack (P-L, byte-stable across runs) is:
  HookedUpdate -> [engine update dispatcher 0x16c7a0]
    -> 0x36eb39   (entity-init fn; carries "dummy_no_ai"/"player"/8 entity GUIDs)
    -> 0x36ff17   (the frame between entity-init and the focus poll)
    -> 0x36af90   (window/focus poll, RVA 0x865fb4 region — PROVEN bounded 5x)
    -> SleepEx
The focus poll is bounded, so the INFINITE repetition is an OUTER loop in 0x36eb39
(or 0x36ff17) re-running the chain on a completion condition that never flips.

NOTE: 0x36eb39 and 0x36ff17 are RETURN-INTO RVAs from the live stack (the address
the call returns to), so the CALL instruction sits just before each. We:
  1. find each function's entry (scan back to the 0xCC padding boundary),
  2. disasm the full body,
  3. flag every BACK-EDGE (a backward conditional/uncond jump = a loop),
  4. for the back-edge that encloses the call to 0x36ff17 / the focus-poll region,
     dump the instructions feeding its test (cmp/test + the load that sets the flag),
  5. resolve rip-relative loads/stores around the loop head (the spun-on global),
     and any string refs (to name the loop's purpose).

Outcome map (pre-committed, flat):
  - back-edge found, its test reads a MEMORY flag/counter (rip-rel global or [reg+off])
      -> that address is the completion condition the swap leaves never-satisfied
      -> next: identify that global / struct field; is it something the FS takeover writes?
  - back-edge test reads a RETURN VALUE from the call to 0x36ff17
      -> 0x36ff17 is the predicate; read ITS body next for what it returns false on.
  - no enclosing back-edge in 0x36eb39's body
      -> the loop is higher (the caller of 0x36eb39); widen to that frame.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
IB = 0x180000000

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

def read_cstr(rva, maxn=200):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off + maxn]
    end = raw.find(b'\x00')
    if end <= 0: return None
    try:
        s = raw[:end].decode('ascii', errors='ignore')
        return s if s.isprintable() and len(s) > 2 else None
    except Exception:
        return None

def find_entry(ret_rva, back=0x6000):
    """Scan back from ret_rva to the last 0xCC..0xCC padding run = function start."""
    off = rva_off(ret_rva)
    if off is None: return None
    raw = pe.__data__[off - back:off + 16]
    best = None; i = 0
    while i < len(raw) - 3:
        if raw[i] == 0xcc and raw[i+1] == 0xcc:
            j = i
            while j < len(raw) and raw[j] == 0xcc: j += 1
            if j < len(raw): best = (off - back) + j
        i += 1
    if best is None: return None
    for s in pe.sections:
        if s.PointerToRawData <= best < s.PointerToRawData + s.SizeOfRawData:
            return s.VirtualAddress + (best - s.PointerToRawData)
    return None

def rip_target(insn):
    """Resolve a rip-relative operand to an RVA, or None."""
    op = insn.op_str
    if 'rip + ' in op:
        try: disp = int(op.split('rip + ')[1].split(']')[0].strip(), 16)
        except Exception: return None
    elif 'rip - ' in op:
        try: disp = -int(op.split('rip - ')[1].split(']')[0].strip(), 16)
        except Exception: return None
    else:
        return None
    return (insn.address + insn.size - IB) + disp

# CORRECTION (2026-06-21): the cdb frames are `ffxFsr2ResourceIsNull (export RVA
# 0x4fb100) + offset` — a NEAREST-EXPORT label (handoff §2.6 says discount it), so
# the bare `0x36eb39`/`0x36ff17` are NOT raw RVAs. Real RVA = 0x4fb100 + offset.
# Sanity: 0x4fb100 + 0x36af90 = 0x866090 = Main's confirmed focus-poll RIP (FINDINGS).
# So the prior "0x36eb39 = entity-init" read disassembled the WRONG (raw-RVA) function.
EXPORT_FFX = 0x4fb100
TARGETS = [
    ('REAL 0x869c39 (=ffx+0x36eb39, the wedge-stack frame)', EXPORT_FFX + 0x36eb39),
    ('REAL 0x86b017 (=ffx+0x36ff17, frame above focus poll)', EXPORT_FFX + 0x36ff17),
]

for name, ret_rva in TARGETS:
    print(f'\n{"="*78}\n== {name}  RVA 0x{ret_rva:x}\n{"="*78}')
    entry = find_entry(ret_rva)
    if entry is None:
        print('   (no entry found)'); continue
    # disasm from entry to a bit past the return-into point
    span = (ret_rva - entry) + 0x140
    if span <= 0 or span > 0x6000: span = 0x2000
    print(f'   entry ~0x{entry:x}   body span 0x{span:x}   (call site ~0x{ret_rva-5:x})')
    try:
        data = pe.get_data(entry, span)
    except Exception as e:
        print(f'   (read failed: {e})'); continue

    insns = list(md.disasm(data, IB + entry))
    backedges = []
    for insn in insns:
        rva = insn.address - IB
        if insn.mnemonic.startswith('j') and insn.op_str.startswith('0x'):
            try:
                t = int(insn.op_str, 16) - IB
                if t < rva and (rva - t) < 0x2000:
                    backedges.append((rva, t, insn.mnemonic))
            except Exception:
                pass

    print(f'\n   --- BACK-EDGES (loops) in body: {len(backedges)} ---')
    for rva, t, mn in backedges:
        enclose = ' <== ENCLOSES the return-into point' if t <= ret_rva <= rva else ''
        print(f'      0x{rva:08x} {mn} -> 0x{t:08x}{enclose}')

    # Full annotated listing of the body region near the call + any enclosing loop head.
    print(f'\n   --- annotated body (string refs, rip-globals, cmp/test, calls) ---')
    for insn in insns:
        rva = insn.address - IB
        note = ''
        if rva == ret_rva - 5 or (rva < ret_rva <= rva + insn.size):
            note = '  <== CALL site (return-into is the wedge frame)'
        tgt = rip_target(insn)
        if tgt is not None:
            s = read_cstr(tgt)
            if s: note += f'  ; -> "{s}"'
            else: note += f'  ; -> rva 0x{tgt:x}'
        if insn.mnemonic in ('cmp', 'test'):
            note += '  [TEST]'
        # only print interesting lines + the immediate neighborhood of back-edges/calls
        interesting = (insn.mnemonic in ('cmp','test','call') or
                       insn.mnemonic.startswith('j') or 'rip' in insn.op_str or note)
        if interesting:
            print(f'      0x{rva:08x} {insn.mnemonic:<7}{insn.op_str}{note}')
