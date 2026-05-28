"""produce_caller_reg_args.py -- the caller-side REGISTER-arg estimate pass
(parallel-ghidra-research.md §4e B; the salvaged register-only win).

WHAT THIS IS (and explicitly is NOT)
------------------------------------
verified static arity was FALSIFIED by probe_caller_arity.py (Outcome C): ~30%
of functions have no direct caller, and the STACK-arg side of a caller-window
scan is garbage (caller locals/spills read as 13/18/24-arg noise). What survives
is the REGISTER-arg side: scan backward from each callsite and count which of the
integer arg registers rcx/rdx/r8/r9 (+ their sub-registers, + xmm0-3) get written
before the call. That count is a NON-AUTHORITATIVE TIGHTER FLOOR -- exact only for
<=4-arg functions (a 5+ -arg call also passes args on the stack, invisible here).
It is NOT verified arity. The stack side is DROPPED entirely on purpose.

CALLER INDEX (built ONCE up front)
----------------------------------
A target_va -> [callsite_va, ...] index built via the recovered
`_research/phase6-save-load/phase6_find_callers.py` E8 imm32 direct-call scan.
The index spans the FULL binary (a caller may be out of the worker's RVA range),
giving O(1) per-function lookup.

PER FUNCTION
------------
For each function WITH direct callers: at each callsite, scan backward over a
bounded window (~64 insns, stopping at the previous call/jmp/ret boundary) and
record the highest arg-register POSITION written (rcx=1, rdx=2, r8=3, r9=4),
capped at 4 by construction. Aggregate across callsites = MAX; agreement =
"agree" if all callsites agreed, else "spread:MIN..MAX".

Functions with NO direct callers: emit NO row (they fall back to the Java
functions/ floor) -- counted as no_callers.

OUTPUT (a SEPARATE table, merged OVER signatures/ BY RVA)
---------------------------------------------------------
RVA-sharded `caller_reg_args/`:

    module, game_version, rva, caller_reg_arg_count, caller_count, agreement,
    edge_reason

THE MERGE (documented here for the maintainer):
    merged_floor = max(observed_arg_slots [signatures/], caller_reg_arg_count)
    abi_confidence becomes "count+width+caller_reg" -- a DISTINCT, NON-authoritative
    tier. HOOK_SIG_GATE must NEVER crash-gate against it: it is a tighter FLOOR,
    exact only for <=4-arg functions. NO value reads as "exactly N args".

CROSS-CHECK (the verified anchors):
    FUN_180001050 (rva 0x1050) -> caller_reg_arg_count = 3 (rcx/rdx/r8 set at its
        one callsite 0x1243 inside FUN_1800011d0).
    SaveGame (rva 0x3581b04)   -> no direct callers -> NO row.

SHARD SCHEME: matches the Java ShardWriter (shardOf=rva//0x100000,
`caller_reg_args_<startRva:08x>.csv`, header per shard, RFC-4180 quoting).

AP14: no-caller (NO row, counted as no_callers); a backward-scan that decodes
nothing -> a visible edge row, edge_reason=no_window, counted. Summary asserts
emitted + edge + no_callers == scanned.

RVA-RANGE FILTER: [rvaStart, rvaEnd) half-open, optional (absent = all). Filters
on the TARGET function's rva (the row owner). Heartbeat every 10000 functions.

RUN
---
    python produce_caller_reg_args.py <dll> <functions_csv> <out_dir> \\
        [limit] [rvaStart] [rvaEnd]
"""

import csv
import os
import sys

import capstone
import pefile

SHARD_SPAN = 0x100000
HEADER = ("module,game_version,rva,caller_reg_arg_count,caller_count,agreement,"
          "edge_reason")

WINDOW_INSNS = 64

# Arg-register POSITION map: each integer arg reg + its sub-registers, and the
# first four xmm regs. Position = which MSVC x64 arg slot the write feeds.
ARG_REG_POS = {}
for pos, regs in [
    (1, ("rcx", "ecx", "cx", "cl", "ch", "xmm0")),
    (2, ("rdx", "edx", "dx", "dl", "dh", "xmm1")),
    (3, ("r8", "r8d", "r8w", "r8b", "xmm2")),
    (4, ("r9", "r9d", "r9w", "r9b", "xmm3")),
]:
    for r in regs:
        ARG_REG_POS[r] = pos

BOUNDARY_MNEMONICS = {"ret", "retn", "retf", "int3"}


def csv_q(s):
    if s is None:
        s = ""
    if any(c in s for c in (",", '"', "\n", "\r")):
        return '"' + s.replace('"', '""') + '"'
    return s


class Image:
    def __init__(self, dll):
        self.pe = pefile.PE(dll, fast_load=True)
        self.image_base = self.pe.OPTIONAL_HEADER.ImageBase
        self.text_va = None
        self.text_data = None
        for sec in self.pe.sections:
            name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
            if name == ".text":
                self.text_va = self.image_base + sec.VirtualAddress
                self.text_data = bytes(sec.get_data())
        if self.text_data is None:
            raise SystemExit("no .text section in image")
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def build_caller_index(self):
        """target_va -> [callsite_va] for every E8 imm32 direct call (full binary,
        phase6_find_callers logic)."""
        td = self.text_data
        tva = self.text_va
        index = {}
        n = len(td)
        for i in range(n - 5):
            if td[i] != 0xE8:
                continue
            disp = int.from_bytes(td[i + 1:i + 5], "little", signed=True)
            site = tva + i
            target = site + 5 + disp
            index.setdefault(target, []).append(site)
        return index

    def reg_args_at_callsite(self, callsite_va):
        """Backward-scan a bounded window before the callsite; return the highest
        arg-register position written (0..4), or None if the window decoded
        nothing. Linear disassembly forward from a window start, keeping only the
        insns strictly before the callsite and after the last basic-block
        boundary, then attribute writes to arg regs."""
        tva = self.text_va
        td = self.text_data
        # Decode forward from a generous byte window so x86 insn boundaries align,
        # then keep the tail of insns ending at the callsite.
        win_bytes = WINDOW_INSNS * 8
        start = callsite_va - win_bytes
        if start < tva:
            start = tva
        off = start - tva
        insns = []
        for ins in self.md.disasm(td[off:callsite_va - tva], start):
            insns.append(ins)
        if not insns:
            return None
        # Keep only the last WINDOW_INSNS, and cut at the last bb boundary (a
        # prior call/unconditional jmp/ret terminates the caller's arg setup).
        insns = insns[-WINDOW_INSNS:]
        cut = 0
        for i in range(len(insns) - 1, -1, -1):
            mn = insns[i].mnemonic.lower()
            if mn in BOUNDARY_MNEMONICS or mn == "call" or mn == "jmp":
                cut = i + 1
                break
        window = insns[cut:]
        highest = 0
        for ins in window:
            ops = ins.operands
            if not ops:
                continue
            # Destination (write) operand = first operand for the writing insns
            # we care about. We only attribute REGISTER destinations.
            dst = ops[0]
            if dst.type != capstone.x86.X86_OP_REG:
                continue
            mn = ins.mnemonic.lower()
            # mov/lea/movzx/movsx/movsxd/xor/add/sub/... write op0. Skip cmp/test
            # (no write). This is a floor estimate, not exact dataflow.
            if mn in ("cmp", "test", "push", "jmp", "ret"):
                continue
            rname = ins.reg_name(dst.reg)
            pos = ARG_REG_POS.get(rname)
            if pos and pos > highest:
                highest = pos
        return highest


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: produce_caller_reg_args.py <dll> <functions_csv> "
                 "<out_dir> [limit] [rvaStart] [rvaEnd]")
    dll = sys.argv[1]
    functions_csv = sys.argv[2]
    out_dir = sys.argv[3]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    rva_start = int(sys.argv[5], 16) if len(sys.argv) > 5 else None
    rva_end = int(sys.argv[6], 16) if len(sys.argv) > 6 else None
    if (rva_start is None) != (rva_end is None):
        sys.exit("pass rvaStart and rvaEnd together (half-open [start, end)).")

    table_dir = os.path.join(out_dir, "caller_reg_args")
    os.makedirs(table_dir, exist_ok=True)

    img = Image(dll)
    image_base = img.image_base
    caller_index = img.build_caller_index()

    shards = {}

    def shard_writer(rva):
        idx = rva // SHARD_SPAN
        f = shards.get(idx)
        if f is None:
            name = "caller_reg_args_%08x.csv" % (idx * SHARD_SPAN)
            f = open(os.path.join(table_dir, name), "w", encoding="utf-8", newline="")
            f.write(HEADER + "\n")
            shards[idx] = f
        return f

    scanned = 0
    emitted = 0
    edge = 0
    no_callers = 0

    with open(functions_csv, "r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            try:
                rva = int(row["rva"], 16)
            except (KeyError, ValueError):
                continue
            if rva_start is not None and not (rva_start <= rva < rva_end):
                continue  # out-of-scope; not counted (AP14).
            if limit > 0 and scanned >= limit:
                break
            scanned += 1

            target_va = image_base + rva
            callsites = caller_index.get(target_va, [])
            if not callsites:
                no_callers += 1
                continue  # NO row -- falls back to the Java floor.

            module = row.get("module", "")
            game_version = row.get("game_version", "")

            counts = []
            any_window = False
            for cs in callsites:
                c = img.reg_args_at_callsite(cs)
                if c is None:
                    continue
                any_window = True
                counts.append(c)

            if not any_window:
                # Direct callers exist but no callsite window decoded -- visible
                # edge row (AP14).
                edge += 1
                cells = [csv_q(module), csv_q(game_version), csv_q("0x%x" % rva),
                         "", str(len(callsites)), "", csv_q("no_window")]
                shard_writer(rva).write(",".join(cells) + "\n")
            else:
                agg = max(counts)
                lo, hi = min(counts), max(counts)
                agreement = "agree" if lo == hi else "spread:%d..%d" % (lo, hi)
                emitted += 1
                cells = [csv_q(module), csv_q(game_version), csv_q("0x%x" % rva),
                         str(agg), str(len(callsites)), csv_q(agreement), ""]
                shard_writer(rva).write(",".join(cells) + "\n")

            if scanned % 10000 == 0:
                print("[produce_caller_reg_args] scanned %d functions..." % scanned,
                      flush=True)

    for f in shards.values():
        f.close()

    print("-" * 70, flush=True)
    print("produce_caller_reg_args: %d scanned, %d rows, %d edge, %d no_callers"
          % (scanned, emitted, edge, no_callers), flush=True)
    print("  shards written: %d" % len(shards), flush=True)
    assert emitted + edge + no_callers == scanned, \
        "ACCOUNTING: emitted(%d)+edge(%d)+no_callers(%d) != scanned(%d)" \
        % (emitted, edge, no_callers, scanned)
    print("  ACCOUNTING OK: emitted+edge+no_callers == scanned", flush=True)


if __name__ == "__main__":
    main()
