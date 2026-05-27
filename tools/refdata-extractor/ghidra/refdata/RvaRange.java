// RvaRange.java -- a half-open [start, end) RVA filter shared by the three Java
// extraction passes (FunctionPass / StatementPass / CallEdgePass).
//
// ONE CONCERN: decide whether a function's rva is THIS worker's job. Built for
// the re-run-infrastructure cycle (parallel-ghidra-research.md §8 "Step-3 run
// pace + parallelism"): RESUME (re-run only the not-yet-done RVA range after a
// crash) and PARALLELISM (R2's N workers each over a disjoint range). This is
// R1 -- the range filter only; the parallel orchestrator/project-copy/merge is R2.
//
// HALF-OPEN, EXACT (load-bearing): a function is in-range iff
//   rvaStart <= rva < rvaEnd
// start INCLUSIVE, end EXCLUSIVE. Adjacent ranges [a,b) and [b,c) thus PARTITION
// the rva line with no overlap and no gap -- the property R2's parallel merge
// depends on (disjoint ranges -> disjoint shard files via ShardWriter.shardOf,
// no row-level dedup). The filter is a PURE skip predicate: a function outside
// the range is skipped ENTIRELY (no row in ANY table), and is NOT counted in any
// pass's accounting tallies (a skipped function is out-of-scope, not an edge
// state -- AP14: do NOT log/count an out-of-scope function as a failure).
//
// DEFAULT = the FULL range (no filter): the no-range default is byte-identical
// to the current all-functions behavior. full() admits every rva.

package refdata;

public final class RvaRange {

    private final long start; // inclusive
    private final long end;   // exclusive

    private RvaRange(long start, long end) {
        this.start = start;
        this.end = end;
    }

    /** The unbounded range: admits every rva (the production default). */
    public static RvaRange full() {
        return new RvaRange(Long.MIN_VALUE, Long.MAX_VALUE);
    }

    /**
     * A bounded half-open range [start, end). {@code start} inclusive,
     * {@code end} exclusive. {@code end <= start} is an empty range (admits
     * nothing) -- a degenerate worker range is honest (it processes nothing),
     * not an error.
     */
    public static RvaRange of(long start, long end) {
        return new RvaRange(start, end);
    }

    /** True iff {@code rva} is in [start, end) -- this worker's job. */
    public boolean contains(long rva) {
        return rva >= start && rva < end;
    }

    public boolean isFull() {
        return start == Long.MIN_VALUE && end == Long.MAX_VALUE;
    }

    public long start() {
        return start;
    }

    public long end() {
        return end;
    }

    /** Human-readable for the run summary / manifest (e.g. "[0x1000, 0x2000)"). */
    @Override
    public String toString() {
        if (isFull()) {
            return "(full -- all functions)";
        }
        return "[0x" + Long.toHexString(start) + ", 0x" + Long.toHexString(end) + ")";
    }
}
