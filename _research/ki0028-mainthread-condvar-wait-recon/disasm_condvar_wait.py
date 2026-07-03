"""KI-0028 — read the main-thread condvar WAIT fn 0x1c1e7e0 + its caller 0x4c2b72.

The 2 byte-identical invasive samples caught Main parked in SleepConditionVariableSRW
with return address WHGame RVA 0x1c1e7e0. THE question (KI-0028 frontier):
  - which condvar/SRW address does 0x1c1e7e0 wait on, and what predicate/loop-exit gates it?
  - is it the SAME std::call_once guard as byte-adjacent 0x1c1e988 (condvar 0x50c5fa8 /
    lock 0x50c5fb0 — the PROBE-M-exonerated same-thread once guard), or a DIFFERENT
    cross-thread wait with a real producer?
  - 0x4c2b72 is the immediate caller (return addr one frame up) — it holds the predicate /
    the loop around the wait and shows what work Main is blocked ON.

Reuse of disasm_gate_fns.py (window-exit-gate-recon). Resolve every rip-global (.data
targets), IAT import names, and direct call edges. Stop at ret/int3 per fn.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories()
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
IB = 0x180000000

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

def sect(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode()
    return "?"

def read_cstr(rva, maxn=120):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off + maxn]
    end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii", errors="ignore")
        return s if s.isprintable() else None
    except Exception:
        return None

iat = {}
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for mod in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in mod.imports:
            if imp.name:
                iat[imp.address] = imp.name.decode()

def rip_target(insn):
    op = insn.op_str
    if "rip + " in op:
        try: disp = int(op.split("rip + ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    elif "rip - " in op:
        try: disp = -int(op.split("rip - ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    else:
        return None
    return insn.address + insn.size + disp

def dump(start_rva, span, label):
    print(f"\n=== {label}: fn 0x{start_rva:x} (span 0x{span:x}) ===")
    data = pe.get_data(start_rva, span)
    for insn in md.disasm(data, IB + start_rva):
        rva = insn.address - IB
        note = ""
        if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
            t = int(insn.op_str, 16) - IB
            note += f"  -> fn 0x{t:x}"
        tgt = rip_target(insn)
        if tgt is not None:
            if insn.mnemonic in ("call", "jmp") and "[rip" in insn.op_str and tgt in iat:
                note += f"  ; IMPORT {iat[tgt]}"
            else:
                trva = tgt - IB
                s = read_cstr(trva)
                note += f"  ; [{sect(trva)}] 0x{trva:x}" + (f' "{s}"' if s else "")
        print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
        if insn.mnemonic in ("ret", "int3"):
            break

def find_fn_start(ret_rva, back=0x600):
    """Scan backward: a function boundary is an int3 pad (0xcc) or a preceding ret (0xc3),
    followed by a byte stream that decodes cleanly all the way through ret_rva. Return the
    earliest such aligned start so we see the FULL body (incl. the SleepConditionVariableSRW
    call ABOVE the return address)."""
    for start in range(ret_rva - back, ret_rva):
        prev = pe.get_data(start - 1, 1)[0]
        if prev not in (0xcc, 0xc3):   # must follow an int3 pad or a ret
            continue
        data = pe.get_data(start, ret_rva - start + 1)
        landed = False
        for insn in md.disasm(data, IB + start):
            a = insn.address - IB
            if a == ret_rva:
                landed = True
                break
            if a > ret_rva:
                break
        if landed:
            return start
    return ret_rva

def dump_full(start_rva, end_rva, label):
    """Dump start..end (a whole function), not stopping at the first ret."""
    print(f"\n=== {label}: fn 0x{start_rva:x} .. 0x{end_rva:x} ===")
    span = end_rva - start_rva
    data = pe.get_data(start_rva, span)
    for insn in md.disasm(data, IB + start_rva):
        rva = insn.address - IB
        marker = "  <== RETURN ADDR (Main here)" if rva == RET_MARK else ""
        note = ""
        if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
            t = int(insn.op_str, 16) - IB
            note += f"  -> fn 0x{t:x}"
        tgt = rip_target(insn)
        if tgt is not None:
            if insn.mnemonic in ("call", "jmp") and "[rip" in insn.op_str and tgt in iat:
                note += f"  ; IMPORT {iat[tgt]}"
            else:
                trva = tgt - IB
                s = read_cstr(trva)
                note += f"  ; [{sect(trva)}] 0x{trva:x}" + (f' "{s}"' if s else "")
        print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}{marker}")

import sys
# Caller chain (stack RetAddrs, resolved): 0x1c1e7e0 (wait) -> 0x4c2b72 -> 0x1de92e9
# -> 0x9aceb8 -> ... The predicate + cv object live in the OWNER frame. Dump each
# caller's full body from a scanned-back start through its return address.
def dump_caller(ret_rva, back, label):
    # scan back for an int3/ret-preceded aligned start that decodes onto ret_rva
    start = None
    for s in range(ret_rva - back, ret_rva):
        prev = pe.get_data(s - 1, 1)[0]
        if prev not in (0xcc, 0xc3):
            continue
        data = pe.get_data(s, ret_rva - s + 1)
        for insn in md.disasm(data, IB + s):
            a = insn.address - IB
            if a == ret_rva:
                start = s; break
            if a > ret_rva:
                break
        if start is not None:
            break
    if start is None:
        start = ret_rva - 0x40
    globals()['RET_MARK'] = ret_rva
    dump_full(start, ret_rva + 0x20, f"{label} (caller 0x{ret_rva:x}, start 0x{start:x})")

target = sys.argv[1] if len(sys.argv) > 1 else "wait"
if target == "wait":
    RET_MARK = 0x1c1e7e0
    dump_full(0x1c1e700, 0x1c1e828, "WAIT ROUTINE (Sleep call + predicate loop, 0x1c1e700..)")
elif target == "callers":
    dump_caller(0x4c2b72, 0x120, "L1 wait_for wrapper")
    dump_caller(0x1de92e9, 0x400, "L2")
    dump_caller(0x9aceb8, 0x400, "L3 (predicate owner?)")
elif target == "owner":
    # Full owner routine 0x9ace78 sits in — scan back hard for the prologue, dump the
    # whole body so we see rbx's construction, the [0x492b8c8]+0x720 kick, and the loop.
    RET_MARK = 0x9aceb8
    start = None
    for s in range(0x9ace78 - 0x800, 0x9ace78):
        prev = pe.get_data(s - 1, 1)[0]
        if prev not in (0xcc, 0xc3):
            continue
        data = pe.get_data(s, 0x9aceb8 - s + 1)
        for insn in md.disasm(data, IB + s):
            a = insn.address - IB
            if a == 0x9aceb8:
                start = s; break
            if a > 0x9aceb8:
                break
        if start is not None:
            break
    if start is None: start = 0x9ace00
    dump_full(start, 0x9acf00, f"OWNER routine (start 0x{start:x})")

