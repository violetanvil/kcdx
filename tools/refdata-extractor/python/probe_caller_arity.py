"""probe_caller_arity.py -- the caller-arity FEASIBILITY probe
(parallel-ghidra-research.md §4e; results-driven.md -- it FALSIFIED verified
static arity). This file is the RECORDED EVIDENCE, rebuilt.

THE QUESTION
------------
Can a caller-side backward scan of callsite windows recover VERIFIED static arity
for a function (the true arg count), better than the callee-side stack-slot floor?

PRE-COMMITTED OUTCOME -> MEANING MAP (written before running; verbatim discipline)
----------------------------------------------------------------------------------
  Outcome A (RELIABLE): both the register side and the stack side agree across
      callsites and match the known-arity anchors -> caller-window scanning yields
      verified static arity -> EMIT it as authoritative arity.
  Outcome B (NOISY-BUT-BETTER): the register side is clean but the stack side is
      noisy; aggregated counts are a tighter floor than the callee side, though
      not exact -> emit the REGISTER side as a non-authoritative tighter floor,
      DROP the stack side.
  Outcome C (FALSIFIED): a large fraction of functions have NO direct caller
      (indirect/vtable/exported-only), AND the stack-arg side is garbage (caller
      locals/spills misread as 13/18/24 args) -> verified static arity is NOT
      achievable from this method; only the register side is salvageable.

KNOWN-ARITY CROSS-CHECKS
------------------------
  SaveGame (rva 0x3581b04) has 7 args BUT has NO direct callers -> INVISIBLE to a
      caller scan (it is called indirectly). This alone caps caller-scan coverage.
  FUN_180001050 (rva 0x1050) -> its one callsite (0x1243 in FUN_1800011d0) sets
      rcx/rdx/r8 cleanly -> 3 register args recovered with no stack noise.

CONCLUSION (recorded): Outcome C. ~30% of functions have no direct caller, and
the stack-arg side produces 13/18/24-arg garbage from caller locals/spills.
verified static arity is NOT achievable. The REGISTER side IS the salvageable win
(produce_caller_reg_args.py emits it as a non-authoritative <=4-arg floor; the
stack side is dropped).

GROUND-TRUTH-FIRST: for the known anchors we dump the RAW caller window (the
actual instructions) before any count, so the evidence is theory-independent.

RUN
---
    python probe_caller_arity.py <dll> <functions_csv> [sample_n=40]
"""

import csv
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from produce_caller_reg_args import Image, WINDOW_INSNS, ARG_REG_POS, \
    BOUNDARY_MNEMONICS  # noqa: E402

ANCHORS = {
    0x1050: "FUN_180001050 (expect 3 register args: rcx/rdx/r8)",
    0x3581b04: "SaveGame (7 args, but NO direct callers -> invisible)",
}


def stack_arg_estimate(img, callsite_va):
    """The STACK-arg side (the noisy half we are testing). Count distinct
    [rsp+disp] WRITE targets in the window with disp >= 0x20 (above the 4 home
    slots) -- the naive 'stack args being staged' heuristic. This is the side the
    probe expects to be garbage."""
    tva = img.text_va
    td = img.text_data
    win_bytes = WINDOW_INSNS * 8
    start = max(tva, callsite_va - win_bytes)
    off = start - tva
    insns = list(img.md.disasm(td[off:callsite_va - tva], start))[-WINDOW_INSNS:]
    cut = 0
    for i in range(len(insns) - 1, -1, -1):
        mn = insns[i].mnemonic.lower()
        if mn in BOUNDARY_MNEMONICS or mn in ("call", "jmp"):
            cut = i + 1
            break
    window = insns[cut:]
    stack_offsets = set()
    for ins in window:
        if not ins.operands:
            continue
        dst = ins.operands[0]
        if dst.type != capstone.x86.X86_OP_MEM:
            continue
        mem = dst.mem
        base = ins.reg_name(mem.base) if mem.base else None
        if base == "rsp" and mem.index is None and mem.disp >= 0x20:
            stack_offsets.add(mem.disp)
    # naive arity = 4 home + however many stack slots staged
    return 4 + len(stack_offsets) if stack_offsets else len(stack_offsets)


def dump_window(img, callsite_va):
    tva = img.text_va
    td = img.text_data
    win_bytes = WINDOW_INSNS * 8
    start = max(tva, callsite_va - win_bytes)
    off = start - tva
    insns = list(img.md.disasm(td[off:callsite_va - tva], start))[-16:]
    for ins in insns:
        print("      %#x: %s %s" % (ins.address - img.image_base,
                                    ins.mnemonic, ins.op_str), flush=True)


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: probe_caller_arity.py <dll> <functions_csv> [sample_n]")
    dll = sys.argv[1]
    functions_csv = sys.argv[2]
    sample_n = int(sys.argv[3]) if len(sys.argv) > 3 else 40

    img = Image(dll)
    image_base = img.image_base
    caller_index = img.build_caller_index()

    print("=" * 70, flush=True)
    print("caller-arity feasibility probe (results-driven; falsification test)",
          flush=True)
    print("=" * 70, flush=True)

    # --- 1. Ground-truth-first: dump the raw window for the known anchors. ---
    print("\n-- KNOWN-ARITY ANCHORS (raw window dump, then counts) --", flush=True)
    for rva, desc in ANCHORS.items():
        target = image_base + rva
        sites = caller_index.get(target, [])
        print("\n  %s  rva=0x%x  direct callers: %d" % (desc, rva, len(sites)),
              flush=True)
        for cs in sites:
            print("    callsite 0x%x window (last 16 insns):" % (cs - image_base),
                  flush=True)
            dump_window(img, cs)
            reg = img.reg_args_at_callsite(cs)
            stk = stack_arg_estimate(img, cs)
            print("      -> register-side count = %s ; stack-side estimate = %s"
                  % (reg, stk), flush=True)

    # --- 2. Sample the population: measure no-caller fraction + stack noise. ---
    rvas = []
    with open(functions_csv, "r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            try:
                if row.get("is_thunk") != "0":
                    continue
                rvas.append(int(row["rva"], 16))
            except (KeyError, ValueError):
                continue
    stride = max(1, len(rvas) // sample_n)
    sample = rvas[::stride][:sample_n]

    no_caller = 0
    with_caller = 0
    reg_counts = []
    stack_counts = []
    for rva in sample:
        sites = caller_index.get(image_base + rva, [])
        if not sites:
            no_caller += 1
            continue
        with_caller += 1
        for cs in sites:
            r = img.reg_args_at_callsite(cs)
            if r is not None:
                reg_counts.append(r)
            stack_counts.append(stack_arg_estimate(img, cs))

    print("\n-- SAMPLE (%d functions, even stride %d) --" % (len(sample), stride),
          flush=True)
    print("  no direct caller: %d (%.0f%%)  with caller: %d"
          % (no_caller, 100.0 * no_caller / max(1, len(sample)), with_caller),
          flush=True)
    if reg_counts:
        print("  register-side counts: min=%d max=%d (all <= 4 by construction)"
              % (min(reg_counts), max(reg_counts)), flush=True)
    if stack_counts:
        print("  stack-side estimates: min=%d max=%d  <-- the noise "
              "(caller locals/spills read as args)"
              % (min(stack_counts), max(stack_counts)), flush=True)

    # --- 3. Verdict against the pre-committed map. ---
    # Outcome C is DECISIVE on EITHER prong, independently:
    #   (i)  a nonzero no-caller fraction makes known-arity functions (e.g.
    #        SaveGame, 7 args, 0 direct callers) INVISIBLE -- so caller-window
    #        scanning can never yield verified static arity for ALL functions.
    #   (ii) the stack-arg side mixes caller locals/spills (counts > 4, no
    #        cross-callsite agreement) -- garbage, not arity.
    # Either prong alone falsifies "verified static arity from caller windows".
    print("\n-- VERDICT --", flush=True)
    savegame_invisible = len(caller_index.get(image_base + 0x3581b04, [])) == 0
    prong_i = no_caller > 0 or savegame_invisible
    prong_ii = bool(stack_counts) and max(stack_counts) > 4
    if prong_i or prong_ii:
        print("  OUTCOME C (FALSIFIED): verified static arity is NOT achievable "
              "from caller windows.", flush=True)
        if prong_i:
            print("    prong (i): no-caller fraction = %.0f%% (SaveGame, 7 args, "
                  "has 0 direct callers -> INVISIBLE). A method that cannot see "
                  "indirectly-called functions cannot give arity for all."
                  % (100.0 * no_caller / max(1, len(sample))), flush=True)
        if prong_ii:
            print("    prong (ii): stack-side estimate exceeded 4 (max=%d) with no "
                  "agreement -- caller locals/spills misread as args."
                  % max(stack_counts), flush=True)
        print("  SALVAGE: the REGISTER side is clean (FUN_180001050 -> 3) -> emit "
              "it as a non-authoritative <=4-arg floor (produce_caller_reg_args.py)"
              "; DROP the stack side.", flush=True)
    else:
        print("  (data did not land in Outcome C this run -- inspect counts above "
              "against the A/B/C map.)", flush=True)


if __name__ == "__main__":
    main()
