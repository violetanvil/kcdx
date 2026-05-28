"""produce_signatures.py -- the abi_walker SIGNATURE pass for the production
reference-data extractor (parallel-ghidra-research.md §4e A; the
HONEST-WIDTH-TYPED signature decision).

WHAT IT EMITS
-------------
An RVA-sharded `signatures/` table the maintainer merges OVER the Java side's
functions/ table BY RVA -- one row per in-range function:

    module, game_version, rva, signature, signature_source, abi_confidence,
    observed_arg_slots, edge_reason

signature_source = "abi_walker" (WINS over the Java floor's "ghidra" at merge).
abi_confidence  = "count+width".

THE HONEST-FLOOR DECISION (load-bearing -- this is the AP2 the walker prevents)
-------------------------------------------------------------------------------
A body-wide stack/register scan can prove a slot's ACCESS WIDTH but CANNOT prove
its semantic type. So the signature is WIDTH-TYPED ONLY:

    8-byte gpr slot  -> i64     4 -> i32     2 -> i16     1 -> i8
    xmm/float slot 8 -> f64     4 -> f32

The return is UNKNOWN -- emitted as the literal `?`. We NEVER emit ptr / u* /
cstr / wstr: those are unprovable from a stack scan, and fabricating them is
exactly the corruption the abi_walker exists to prevent (the prologue-shape guess
that typed SaveGame as 3-arg when it is 7). Signature string form:

    ? (i64, i32, i64)

observed_arg_slots is an EXPLICIT LOWER-BOUND FLOOR, not verified arity. The raw
stack-slot count over-counts on large frames and under-counts register-resident
args; the column name + the abi_confidence tag flag it as unrefined. A consumer
must NEVER read it as "exactly N args".

THE WALKER
----------
This reuses the per-function walking logic of the sanctioned
`_research/phase6-save-load/phase6_abi_walker.py` (recursive branch-following
capstone disassembly + a prologue scan tracking rsp/rbp/r11 offset-from-entry +
an arg-slot memory-operand pass over entry_rsp offsets 0x08..0x100, which are the
MSVC x64 home + stack slots). It runs IN-PROCESS over the function list: the DLL
is opened ONCE via pefile; we do NOT shell out per function.

SHARD SCHEME (matches the Java ShardWriter exactly)
---------------------------------------------------
shardOf(rva) = rva // 0x100000 ; file `signatures_<startRva:08x>.csv` ;
a header row per shard ; RFC-4180 cell quoting.

AP14 -- EVERY in-range function gets a VISIBLE row, never a silent skip:
  - entry not in an executable section -> empty signature, edge_reason=no_section
  - capstone fails at entry              -> empty signature, edge_reason=disasm_failed
  - walk visited nothing                 -> empty signature, edge_reason=empty_walk
The run summary asserts emitted + edge == total (the silent-skip balance check).

RVA-RANGE FILTER: [rvaStart, rvaEnd) half-open, optional (absent = all). A
function outside the range is skipped BEFORE any emit/count (out-of-scope, not an
edge state). Heartbeat every 10000 functions.

RUN
---
    python produce_signatures.py <dll> <functions_csv> <out_dir> \\
        [limit] [max_insns] [rvaStart] [rvaEnd]

  limit     : cap functions processed within range (quick sample). <=0 = all.
  max_insns : per-function recursive-walk safety bound (default 2000).
  rvaStart/rvaEnd : hex (e.g. 0x100000), paired.
"""

import csv
import sys

import capstone
import pefile

SHARD_SPAN = 0x100000
HEADER = ("module,game_version,rva,signature,signature_source,abi_confidence,"
          "observed_arg_slots,edge_reason")

COND_JMPS = {
    "je", "jne", "jz", "jnz", "jg", "jl", "jge", "jle", "ja", "jb",
    "jae", "jbe", "js", "jns", "jo", "jno", "jp", "jnp", "jpe", "jpo",
    "jcxz", "jecxz", "jrcxz",
}
UNCOND_JMPS = {"jmp"}
TERMINATORS = {"ret", "retn", "retf", "int3"}
PROLOGUE_OK = ("push", "mov", "sub", "lea", "xor", "movsxd")


def csv_q(s):
    """RFC-4180 cell quoting -- mirrors the Java Csv.q."""
    if s is None:
        s = ""
    if any(c in s for c in (",", '"', "\n", "\r")):
        return '"' + s.replace('"', '""') + '"'
    return s


def width_type(size, is_float):
    """Map an access width (bytes) to an HONEST width-type. Unknown -> i64."""
    if is_float:
        return {8: "f64", 4: "f32"}.get(size, "f64")
    return {8: "i64", 4: "i32", 2: "i16", 1: "i8"}.get(size, "i64")


class Walker:
    """In-process abi_walker over a single opened image. The per-function
    walking logic copied faithfully from phase6_abi_walker.py."""

    def __init__(self, dll, max_insns):
        self.pe = pefile.PE(dll, fast_load=True)
        self.image_base = self.pe.OPTIONAL_HEADER.ImageBase
        self.max_insns = max_insns
        self.exec_secs = []  # (sva, data)
        for sec in self.pe.sections:
            if sec.Characteristics & 0x20000000:
                sva = self.image_base + sec.VirtualAddress
                self.exec_secs.append((sva, sec.get_data()))
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def _sec_for(self, va):
        for sva, data in self.exec_secs:
            if sva <= va < sva + len(data):
                return sva, data
        return None, None

    def _disasm_at(self, addr, sva, data):
        off = addr - sva
        if off < 0 or off >= len(data):
            return None
        for ins in self.md.disasm(bytes(data[off:off + 16]), addr):
            return ins
        return None

    def walk(self, entry_va):
        """Return (signature_str, observed_arg_slots, edge_reason).
        edge_reason == "" on success."""
        sva, data = self._sec_for(entry_va)
        if data is None:
            return "", 0, "no_section"

        # Recursive branch-following walk (phase6_abi_walker logic).
        visited = set()
        queue = [entry_va]
        entry_addr = entry_va
        while queue:
            addr = queue.pop(0)
            if addr in visited:
                continue
            cur = addr
            safety = 0
            while cur not in visited and safety < self.max_insns:
                safety += 1
                ins = self._disasm_at(cur, sva, data)
                if ins is None:
                    break
                visited.add(cur)
                mn = ins.mnemonic.lower()
                if mn in TERMINATORS:
                    break
                if mn in UNCOND_JMPS:
                    if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                        tgt = ins.operands[0].imm
                        if abs(tgt - entry_addr) < 0x200000:
                            queue.append(tgt)
                    break
                if mn in COND_JMPS:
                    if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                        tgt = ins.operands[0].imm
                        if abs(tgt - entry_addr) < 0x200000:
                            queue.append(tgt)
                cur = ins.address + ins.size

        if not visited:
            # capstone produced nothing decodable at the entry.
            if self._disasm_at(entry_va, sva, data) is None:
                return "", 0, "disasm_failed"
            return "", 0, "empty_walk"

        sorted_addrs = sorted(visited)
        extent_end = max(sorted_addrs)
        last_ins = self._disasm_at(extent_end, sva, data)
        if last_ins:
            extent_end += last_ins.size

        # --- PROLOGUE scan: track rsp/rbp/r11 offset-from-entry (phase6 logic) ---
        rsp_off = 0
        rbp_off = None
        r11_off = None
        prologue_insns = []
        cur = entry_addr
        while cur < extent_end:
            ins = self._disasm_at(cur, sva, data)
            if ins is None:
                break
            mn = ins.mnemonic.lower()
            if mn not in PROLOGUE_OK:
                break
            prologue_insns.append(ins)
            if mn == "push":
                rsp_off -= 8
            elif mn == "sub" and ins.op_str.startswith("rsp,"):
                try:
                    rsp_off -= int(ins.op_str.split(",", 1)[1].strip(), 16)
                except ValueError:
                    pass
            elif mn == "mov" and ins.op_str.startswith("rbp, rsp"):
                rbp_off = rsp_off
            elif mn == "lea" and "rbp," in ins.op_str:
                if len(ins.operands) >= 2 and ins.operands[1].type == capstone.x86.X86_OP_MEM:
                    mem = ins.operands[1].mem
                    base_reg = ins.reg_name(mem.base) if mem.base else None
                    if base_reg == "rsp":
                        rbp_off = rsp_off + mem.disp
                    elif base_reg == "rbp" and rbp_off is not None:
                        rbp_off = rbp_off + mem.disp
            elif mn == "mov" and ins.op_str.startswith("r11, rsp"):
                r11_off = rsp_off
            cur = ins.address + ins.size

        prologue_end = (prologue_insns[-1].address + prologue_insns[-1].size
                        if prologue_insns else entry_addr)

        # r11/rbp redefinition inside the body: post-redef [r11/rbp+N] reads are
        # register restores, NOT incoming-arg reads (the callee-save-ish filter).
        r11_redef = None
        rbp_redef = None
        for addr in sorted_addrs:
            if addr < prologue_end:
                continue
            ins = self._disasm_at(addr, sva, data)
            if not ins:
                continue
            mn = ins.mnemonic.lower()
            if mn in ("lea", "mov") and ins.operands and \
                    ins.operands[0].type == capstone.x86.X86_OP_REG:
                dest = ins.reg_name(ins.operands[0].reg)
                if dest == "r11" and r11_redef is None:
                    r11_redef = addr
                if dest == "rbp" and rbp_redef is None:
                    rbp_redef = addr

        # --- arg-slot memory-operand pass: entry_rsp+offset in 0x08..0x100 ---
        # slot -> max access width seen, and whether float-class (xmm).
        slots = {}  # entry_rsp_offset -> (max_width, is_float)

        def note(off, size, is_float):
            w, f = slots.get(off, (0, False))
            slots[off] = (max(w, size), f or is_float)

        for addr in sorted_addrs:
            ins = self._disasm_at(addr, sva, data)
            if not ins:
                continue
            after_r11 = (r11_redef is not None and addr >= r11_redef)
            after_rbp = (rbp_redef is not None and addr >= rbp_redef)
            is_float_ins = ins.mnemonic.lower().startswith(("movs", "movd", "movq",
                "movap", "movup", "adds", "muls", "subs", "divs", "cvts", "comis",
                "ucomis")) and "xmm" in ins.op_str
            for op in ins.operands:
                if op.type != capstone.x86.X86_OP_MEM:
                    continue
                mem = op.mem
                base = ins.reg_name(mem.base) if mem.base else None
                idx = ins.reg_name(mem.index) if mem.index else None
                if idx is not None:
                    continue
                disp = mem.disp
                eoff = None
                if base == "rsp":
                    eoff = rsp_off + disp
                elif base == "rbp" and rbp_off is not None and not after_rbp:
                    eoff = rbp_off + disp
                elif base == "r11" and r11_off is not None and not after_r11:
                    eoff = r11_off + disp
                if eoff is not None and 0x08 <= eoff <= 0x100:
                    note(eoff, op.size, is_float_ins)

        # Build the width-typed positional signature. arg index = off // 8.
        observed = len(slots)
        by_arg = {}
        for off, (w, isf) in slots.items():
            arg_idx = off // 8  # +0x08 -> arg1
            if arg_idx >= 1:
                by_arg[arg_idx] = (w, isf)
        if by_arg:
            top = max(by_arg)
            parts = []
            for i in range(1, top + 1):
                w, isf = by_arg.get(i, (8, False))
                parts.append(width_type(w, isf))
            signature = "? (" + ", ".join(parts) + ")"
        else:
            signature = "? (void)"
        return signature, observed, ""


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: produce_signatures.py <dll> <functions_csv> <out_dir> "
                 "[limit] [max_insns] [rvaStart] [rvaEnd]")
    dll = sys.argv[1]
    functions_csv = sys.argv[2]
    out_dir = sys.argv[3]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    max_insns = int(sys.argv[5]) if len(sys.argv) > 5 else 2000
    rva_start = int(sys.argv[6], 16) if len(sys.argv) > 6 else None
    rva_end = int(sys.argv[7], 16) if len(sys.argv) > 7 else None
    if (rva_start is None) != (rva_end is None):
        sys.exit("pass rvaStart and rvaEnd together (half-open [start, end)).")

    import os
    table_dir = os.path.join(out_dir, "signatures")
    os.makedirs(table_dir, exist_ok=True)

    walker = Walker(dll, max_insns)
    image_base = walker.image_base

    shards = {}  # shard_idx -> open file

    def shard_writer(rva):
        idx = rva // SHARD_SPAN
        f = shards.get(idx)
        if f is None:
            name = "signatures_%08x.csv" % (idx * SHARD_SPAN)
            f = open(os.path.join(table_dir, name), "w", encoding="utf-8", newline="")
            f.write(HEADER + "\n")
            shards[idx] = f
        return f

    total = 0
    emitted = 0  # rows with a non-empty signature
    edge = 0
    edge_kinds = {}

    with open(functions_csv, "r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            try:
                rva = int(row["rva"], 16)
            except (KeyError, ValueError):
                continue
            if rva_start is not None and not (rva_start <= rva < rva_end):
                continue  # out-of-scope: skipped before any count (AP14).
            if limit > 0 and total >= limit:
                break
            total += 1
            module = row.get("module", "")
            game_version = row.get("game_version", "")

            sig, observed, edge_reason = walker.walk(image_base + rva)
            if edge_reason:
                edge += 1
                edge_kinds[edge_reason] = edge_kinds.get(edge_reason, 0) + 1
            else:
                emitted += 1

            cells = [
                csv_q(module), csv_q(game_version),
                csv_q("0x%x" % rva),
                csv_q(sig),
                csv_q("abi_walker"),
                csv_q("count+width"),
                str(observed),
                csv_q(edge_reason),
            ]
            shard_writer(rva).write(",".join(cells) + "\n")

            if total % 10000 == 0:
                print("[produce_signatures] processed %d functions..." % total,
                      flush=True)

    for f in shards.values():
        f.close()

    print("-" * 70, flush=True)
    print("produce_signatures: %d functions, %d signature rows, %d edge rows"
          % (total, emitted, edge), flush=True)
    print("  edge breakdown: %s" % (edge_kinds or "{}"), flush=True)
    print("  shards written: %d" % len(shards), flush=True)
    assert emitted + edge == total, \
        "ACCOUNTING: emitted(%d)+edge(%d) != total(%d)" % (emitted, edge, total)
    print("  ACCOUNTING OK: emitted+edge == total", flush=True)


if __name__ == "__main__":
    main()
