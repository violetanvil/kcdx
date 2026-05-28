// ShardWriter.java -- the RVA-sharded CSV-per-table writer for the production
// reference-data extractor (parallel-ghidra-research.md §4; output-format
// decision: CSV-per-table, RVA-SHARDED).
//
// ONE CONCERN: given a per-table output directory and a function's RVA, route a
// CSV row into the correct RVA-range shard file, lazily opening shards and
// writing each shard's header row once. The per-function pass uses it for the
// `functions/` table; the statement/edge passes instantiate their OWN
// ShardWriter for `statements/`, `referenced_vars/`, `call_edges/` -- the SAME
// shardOf(rva) mapping, so a maintainer imports shard-by-shard with aligned
// boundaries across every table.
//
// SHARD SCHEME (deterministic, reproducible, documented in the manifest):
//   shard index  = rva / SHARD_SPAN          (integer division)
//   shard span   = 0x100000 (1 MiB of RVA)   -- fixed width
//   shard file   = <table>_<startRva:08x>.csv   e.g. functions_00100000.csv
//
//   WHY fixed-width RVA buckets (over "N functions in RVA order"):
//     - A function's shard is a pure function of its RVA alone -- no global
//       ordering pass, no cross-table function-count bookkeeping. The other
//       passes reproduce the exact same fn->shard from RVA with zero shared state.
//     - The boundaries are STABLE across re-runs and across the other tables:
//       statement/edge rows for a function land in the shard matching that
//       function's RVA, so all tables' shard_00100000.csv cover the identical
//       RVA window -- the maintainer imports one window at a time.
//   The filename's startRva = shardIndex * SHARD_SPAN makes the covered window
//   self-documenting.
//
// CRASH-SAFETY + OBSERVABILITY (the incremental-flush fix): each shard's
// PrintWriter is built with autoFlush=true over an explicit UTF-8
// OutputStreamWriter, so every println() lands on disk immediately. This means
// (1) a mid-run JVM death preserves all rows already emitted (vs the prior
// buffer-until-close(), which lost everything on a crash), and (2) a watcher
// sees output grow as the run proceeds. UTF-8 is preserved explicitly (NOT
// FileWriter, which would use the platform default charset -- the CSVs carry
// non-ASCII C++ templated symbols).
//
// Read-only w.r.t. the Ghidra project; this only writes the output CSVs.

package refdata;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

public final class ShardWriter {

    /** Fixed RVA bucket width: one shard per 0x100000 (1 MiB) of RVA. */
    public static final long SHARD_SPAN = 0x100000L;

    private final File tableDir;
    private final String tableName;
    private final String headerRow;
    // shardIndex -> open writer. TreeMap so writtenShards()/close() iterate in
    // RVA order (stable summary + deterministic file set).
    private final TreeMap<Long, PrintWriter> open = new TreeMap<>();

    /**
     * @param tableDir   the per-table output dir (e.g. <out>/functions). Created if absent.
     * @param tableName  the file-stem prefix (e.g. "functions").
     * @param headerRow  the CSV header line written first into every shard.
     */
    public ShardWriter(File tableDir, String tableName, String headerRow) throws IOException {
        this.tableDir = tableDir;
        this.tableName = tableName;
        this.headerRow = headerRow;
        if (!tableDir.isDirectory() && !tableDir.mkdirs() && !tableDir.isDirectory()) {
            throw new IOException("could not create table dir: " + tableDir);
        }
    }

    /** The shard index a given RVA falls into. The SAME mapping every pass reuses. */
    public static long shardOf(long rva) {
        return rva / SHARD_SPAN;
    }

    /** The inclusive start RVA of a shard window (used in the filename). */
    public static long shardStartRva(long shardIndex) {
        return shardIndex * SHARD_SPAN;
    }

    /** Write one already-formatted CSV row into the shard owning {@code rva}. */
    public void writeRow(long rva, String row) {
        long idx = shardOf(rva);
        PrintWriter w = open.get(idx);
        if (w == null) {
            w = openShard(idx);
            open.put(idx, w);
        }
        w.println(row);
    }

    private PrintWriter openShard(long shardIndex) {
        String name = String.format("%s_%08x.csv", tableName, shardStartRva(shardIndex));
        File f = new File(tableDir, name);
        try {
            // autoFlush=true -> every println() flushes to disk (crash-safe +
            // observable). Explicit UTF-8 OutputStreamWriter -- the prior
            // `new PrintWriter(f, "UTF-8")` buffered every row until close()
            // (lost on a crash, invisible mid-run); this uses an explicit UTF-8
            // OutputStreamWriter -- NOT FileWriter, which would regress to the
            // platform default charset.
            PrintWriter w = new PrintWriter(
                new OutputStreamWriter(new FileOutputStream(f), StandardCharsets.UTF_8), true);
            w.println(headerRow);
            return w;
        } catch (IOException e) {
            // A shard we cannot open is a hard failure of the run, not a
            // droppable edge -- surface it (the run would silently lose every
            // function in this RVA window otherwise; AP14).
            throw new RuntimeException("could not open shard " + name, e);
        }
    }

    /** The shard indices written so far (RVA order). */
    public Set<Long> writtenShards() {
        return open.keySet();
    }

    public int shardCount() {
        return open.size();
    }

    /** Flush + close every open shard. */
    public void close() {
        for (Map.Entry<Long, PrintWriter> e : open.entrySet()) {
            e.getValue().flush();
            e.getValue().close();
        }
    }
}
