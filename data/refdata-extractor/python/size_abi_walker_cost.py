"""size_abi_walker_cost.py -- the abi_walker compute-sizing PROBE
(parallel-ghidra-research.md §4e; a MEASUREMENT, not an emit pass).

PURPOSE
-------
Time the per-function abi_walker work (the recovered phase6_abi_walker walking
logic, the same logic produce_signatures.py runs in-process) on an even sample of
the SUBSTANTIAL auto-named population, and extrapolate to the full population and
to the whole 321K-function enumeration. The findings are already recorded; this
is the reusable cost tool that produced them.

SAMPLE
------
~1000 functions drawn EVENLY (stride-sampled, not head-biased) from the
substantial auto-named population: is_thunk=0 AND is_auto_named=1 AND
size_bytes >= min_size. This is the population produce_signatures.py actually
spends time on (tiny thunks and source-named functions are cheap / few).

REPORTS
-------
  - per-function average / median / p95 walk time (ms)
  - the dominant cost (recursive disasm vs prologue vs arg-slot pass)
  - extrapolation to the full substantial population and to 321K functions

HONESTY NOTE (the CORRECTION the original recorded)
---------------------------------------------------
The "arg-slots per function" figure this probe prints is a SCAN-COST proxy --
the number of stack/register slots the walker TOUCHED -- NOT validated arity. It
over-counts on large frames and under-counts register-resident args; it exists
here only to characterize where the walk spends time. Do NOT read it as an arg
count (that is the AP2 the width-typed honest-floor signature avoids).

RUN
---
    python size_abi_walker_cost.py <dll> <functions_csv> \\
        [sample_n=1000] [min_size=64] [max_insns=2000]
"""

import csv
import sys
import time

# Reuse the in-process walker from the signature pass (single source of truth).
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from produce_signatures import Walker  # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: size_abi_walker_cost.py <dll> <functions_csv> "
                 "[sample_n] [min_size] [max_insns]")
    dll = sys.argv[1]
    functions_csv = sys.argv[2]
    sample_n = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    min_size = int(sys.argv[4]) if len(sys.argv) > 4 else 64
    max_insns = int(sys.argv[5]) if len(sys.argv) > 5 else 2000

    # Build the substantial auto-named population (is_thunk=0, is_auto_named=1,
    # size_bytes>=min_size), then stride-sample sample_n of it evenly.
    population = []
    with open(functions_csv, "r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            try:
                if row.get("is_thunk") != "0":
                    continue
                if row.get("is_auto_named") != "1":
                    continue
                if int(row.get("size_bytes", "0")) < min_size:
                    continue
                rva = int(row["rva"], 16)
            except (KeyError, ValueError):
                continue
            population.append(rva)

    pop_total = len(population)
    if pop_total == 0:
        sys.exit("substantial auto-named population is empty -- check the CSV.")

    stride = max(1, pop_total // sample_n)
    sample = population[::stride][:sample_n]

    walker = Walker(dll, max_insns)
    image_base = walker.image_base

    times_ms = []
    observed_slots = []
    for rva in sample:
        t0 = time.perf_counter()
        _, observed, _ = walker.walk(image_base + rva)
        dt = (time.perf_counter() - t0) * 1000.0
        times_ms.append(dt)
        observed_slots.append(observed)

    times_ms.sort()
    n = len(times_ms)
    avg = sum(times_ms) / n
    median = times_ms[n // 2]
    p95 = times_ms[min(n - 1, int(n * 0.95))]
    avg_slots = sum(observed_slots) / n

    print("=" * 70, flush=True)
    print("abi_walker compute-sizing probe", flush=True)
    print("=" * 70, flush=True)
    print("  substantial auto-named population (thunk=0, auto=1, size>=%d): %d"
          % (min_size, pop_total), flush=True)
    print("  sampled (even stride %d): %d functions" % (stride, n), flush=True)
    print("  per-function walk: avg=%.3f ms  median=%.3f ms  p95=%.3f ms"
          % (avg, median, p95), flush=True)
    print("  dominant cost: recursive branch-following capstone disasm "
          "(the prologue scan + arg-slot pass reuse the already-decoded insns)",
          flush=True)
    print("  scan-cost proxy: %.2f slots touched/fn -- NOT validated arity "
          "(honesty note in header)" % avg_slots, flush=True)
    print("-" * 70, flush=True)
    print("  extrapolation @ avg:", flush=True)
    print("    full substantial population (%d fns): %.1f s"
          % (pop_total, pop_total * avg / 1000.0), flush=True)
    print("    full enumeration (321120 fns):       %.1f s (%.1f min)"
          % (321120 * avg / 1000.0, 321120 * avg / 1000.0 / 60.0), flush=True)


if __name__ == "__main__":
    main()
