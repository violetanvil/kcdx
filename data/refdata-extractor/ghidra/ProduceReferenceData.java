// ProduceReferenceData.java -- the production reference-data extractor entry
// point (a Ghidra @category Research GhidraScript). Drives the three per-function
// passes over WHGame.dll and emits the RVA-sharded CSV-per-table dirs the
// maintainer imports into data/reference.sqlite.
//
// WHAT THIS BUILDS (Ghidra side -- parallel-ghidra-research.md §4):
//   functions/         (FunctionPass)   -- per-function rows
//   statements/        (StatementPass)  -- per-statement decomposition
//   referenced_vars/   (StatementPass)  -- per-statement referenced-var storage
//   call_edges/        (CallEdgePass)   -- the caller<->callee graph backbone
// The two Python passes (produce_signatures.py / produce_caller_reg_args.py) emit
// signatures/ + caller_reg_args/ and merge by rva; the harness
// (validate_extractor_output.py) validates the whole 5-table output.
//
// ARGS (getScriptArgs(), positional):
//   [0] out_dir       REQUIRED. The dump root (the table dirs are created under it).
//   [1] version_tag   OPTIONAL. Stamped into game_version. The launcher passes the
//                     sentinel "__none__" for an empty tag (analyzeHeadless.bat
//                     DROPS an empty-string arg, which would shift the positional
//                     contract -- the sentinel is non-empty on the command line
//                     and normalized back to "" here). Default "" = unset.
//   [2] limit         OPTIONAL. Max functions to emit WITHIN the range (quick
//                     sample). -1 / absent = all functions. (The launcher passes
//                     the literal -1 for the same empty-arg-drop reason.)
//   [3] rva_start     OPTIONAL (hex). Inclusive RVA-range start. Paired with [4].
//   [4] rva_end       OPTIONAL (hex). EXCLUSIVE RVA-range end. One without the
//                     other is a hard error (an open-ended worker range is
//                     ambiguous). Absent = full range = all functions.
//
// Run (full binary): -postScript ProduceReferenceData.java <out_dir> <version> -noanalysis -readOnly
// Range worker:      -postScript ProduceReferenceData.java <out_dir> __none__ -1 0x0 0x100000 ...
//
//@category Research

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;

import refdata.CallEdgePass;
import refdata.FunctionPass;
import refdata.RvaRange;
import refdata.ShardWriter;
import refdata.StatementPass;

public class ProduceReferenceData extends GhidraScript {

    // The launcher's empty-version sentinel (non-empty so analyzeHeadless.bat
    // does not drop it + shift the positional args); normalized back to "" here.
    private static final String VERSION_SENTINEL = "__none__";

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("[ProduceReferenceData] ERROR: out_dir arg required.");
            println("  usage: -postScript ProduceReferenceData.java <out_dir> "
                  + "[version_tag] [limit] [rva_start] [rva_end]");
            throw new IllegalArgumentException("out_dir arg required");
        }
        String outDir = args[0];
        String versionTag = (args.length >= 2) ? args[1] : "";
        if (versionTag.equals(VERSION_SENTINEL)) {
            versionTag = ""; // the launcher's empty-tag sentinel -> unset.
        }
        int limit = (args.length >= 3) ? Integer.parseInt(args[2]) : -1;

        // Range: both or neither. One-without-the-other is ambiguous -> hard error.
        RvaRange range;
        boolean hasStart = args.length >= 4 && !args[3].isEmpty();
        boolean hasEnd = args.length >= 5 && !args[4].isEmpty();
        if (hasStart != hasEnd) {
            throw new IllegalArgumentException(
                "rva_start and rva_end must be given together (an open-ended worker "
                + "range is ambiguous)");
        }
        if (hasStart) {
            long start = Long.decode(args[3]);
            long end = Long.decode(args[4]);
            range = RvaRange.of(start, end);
        } else {
            range = RvaRange.full();
        }

        long imageBase = currentProgram.getImageBase().getOffset();
        String module = currentProgram.getName();

        File root = new File(outDir);
        if (!root.isDirectory() && !root.mkdirs() && !root.isDirectory()) {
            throw new IOException("could not create out_dir: " + outDir);
        }
        File functionsDir = new File(root, "functions");
        File statementsDir = new File(root, "statements");
        File referencedVarsDir = new File(root, "referenced_vars");
        File callEdgesDir = new File(root, "call_edges");

        FunctionPass.Stats fnStats;
        StatementPass.Stats stStats;
        CallEdgePass.Stats edStats;

        // --- functions/ pass ---
        ShardWriter functionsOut = new ShardWriter(functionsDir, "functions", FunctionPass.HEADER);
        try {
            fnStats = new FunctionPass(this, functionsOut, versionTag, range).run(limit);
        } finally {
            functionsOut.close();
        }

        // --- statements/ + referenced_vars/ pass ---
        ShardWriter statementsOut = new ShardWriter(statementsDir, "statements", StatementPass.STATEMENTS_HEADER);
        ShardWriter referencedVarsOut = new ShardWriter(referencedVarsDir, "referenced_vars", StatementPass.REFERENCED_VARS_HEADER);
        try {
            stStats = new StatementPass(this, statementsOut, referencedVarsOut, range).run(limit);
        } finally {
            statementsOut.close();
            referencedVarsOut.close();
        }

        // --- call_edges/ pass ---
        ShardWriter edgesOut = new ShardWriter(callEdgesDir, "call_edges", CallEdgePass.HEADER);
        try {
            edStats = new CallEdgePass(this, edgesOut, versionTag, range).run(limit);
        } finally {
            edgesOut.close();
        }

        writeManifest(root, module, imageBase, versionTag, limit, range,
                      functionsOut, statementsOut, edgesOut, fnStats, stStats, edStats);

        // --- run summary ---
        String bar = "========================================================================";
        println(bar);
        println(String.format("[ProduceReferenceData] module=%s  base=0x%x  version=%s%s",
                module, imageBase, versionTag.isEmpty() ? "(unset)" : versionTag,
                range.isFull() ? "  (FULL BINARY)" : ("  range=" + range)));
        println("  out dir            : " + root.getAbsolutePath());
        println(String.format("  functions total    : %d  (clean=%d partial=%d unanalyzable=%d; of which bytes-unreadable=%d)",
                fnStats.total, fnStats.clean, fnStats.partial, fnStats.unanalyzable, fnStats.bytesUnreadable));
        long fnAccounted = fnStats.clean + fnStats.partial + fnStats.unanalyzable;
        if (fnAccounted != fnStats.total) {
            println(String.format("  WARNING: functions row-accounting mismatch -- total=%d but "
                  + "clean+partial+unanalyzable=%d (investigate a silent drop)",
                  fnStats.total, fnAccounted));
        }
        println(String.format("  statements         : %d emitted from %d clean fns "
                + "(partial=%d unanalyzable=%d decompile-edge=%d no-addr-skipped=%d)",
                stStats.statementsEmitted, stStats.cleanFunctions, stStats.partialSkipped,
                stStats.unanalyzableSkipped, stStats.decompileFailed, stStats.noAddrSkipped));
        long stAccounted = stStats.cleanFunctions + stStats.partialSkipped
                + stStats.unanalyzableSkipped + stStats.decompileFailed;
        if (stAccounted != stStats.functionsSeen) {
            println(String.format("  WARNING: statements row-accounting mismatch -- seen=%d but "
                  + "clean+partial+unanalyzable+edge=%d (investigate a silent drop)",
                  stStats.functionsSeen, stAccounted));
        }
        println(String.format("  referenced_vars    : %d rows", stStats.referencedVarsEmitted));
        println(String.format("  call_edges         : %d edges (direct=%d indirect=%d external=%d unresolved=%d; leaves=%d)",
                edStats.edgesEmitted, edStats.resolvedDirect, edStats.indirect,
                edStats.external, edStats.unresolvedTarget, edStats.functionsWithNoCallees));
        long edAccounted = edStats.resolvedDirect + edStats.indirect
                + edStats.external + edStats.unresolvedTarget;
        if (edAccounted != edStats.edgesEmitted) {
            println(String.format("  WARNING: call_edges accounting mismatch -- emitted=%d but "
                  + "resolved+indirect+external+unresolved=%d", edStats.edgesEmitted, edAccounted));
        }
        println(bar);
    }

    private void writeManifest(File root, String module, long imageBase, String versionTag,
                               int limit, RvaRange range, ShardWriter fnsOut, ShardWriter stOut,
                               ShardWriter edOut, FunctionPass.Stats fnStats,
                               StatementPass.Stats stStats, CallEdgePass.Stats edStats)
            throws IOException {
        File mf = new File(root, "_MANIFEST.md");
        PrintWriter w = new PrintWriter(mf, "UTF-8");
        try {
            w.println("# kcdx reference-data dump -- MANIFEST");
            w.println();
            w.println("Produced by `ProduceReferenceData.java` (data/refdata-extractor) "
                    + "+ the two Python passes (signatures/ + caller_reg_args/).");
            w.println();
            w.println("- module        : " + module);
            w.println("- image base    : 0x" + Long.toHexString(imageBase));
            w.println("- game_version  : " + (versionTag.isEmpty() ? "(unset)" : versionTag));
            w.println("- scope         : " + (range.isFull()
                    ? (limit >= 0 ? "SAMPLE limit=" + limit : "FULL BINARY") : ("range " + range)));
            w.println("- functions     : " + fnStats.total + " rows, " + fnsOut.shardCount() + " shards");
            w.println("- statements    : " + stStats.statementsEmitted + " rows, " + stOut.shardCount() + " shards");
            w.println("- call_edges    : " + edStats.edgesEmitted + " rows, " + edOut.shardCount() + " shards");
            w.println();
            w.println("## Output format -- CSV-per-table, RVA-sharded");
            w.println();
            w.println("Each table is a DIRECTORY of shard CSVs (`<table>_<startRva:08x>.csv`, "
                    + "one shard per 0x" + Long.toHexString(ShardWriter.SHARD_SPAN) + " of RVA, "
                    + "keyed on the owning FUNCTION's rva). Every table's `*_<startRva>.csv` "
                    + "covers the identical RVA window -- import one window at a time.");
            w.println();
            w.println("Ghidra side (this tool): `functions/`, `statements/`, `referenced_vars/`, `call_edges/`.");
            w.println("Python side: `signatures/` (produce_signatures.py), `caller_reg_args/` (produce_caller_reg_args.py).");
            w.println();
            w.println("## NOT emitted by the mechanical dump (maintainer-assigned at import)");
            w.println("- `id`, `status` -- maintainer-assigned, append-only, never recycled.");
            w.println("- `subsystem`, `inferred_purpose` -- sparse curated overlay (§4c), left NULL.");
            w.println();
            w.println("## Deferred to Phase 9.3 (NOT in this dump)");
            w.println("- the TRUE live-in `captures` table (referenced_vars/ is the referenced-var "
                    + "approximation, not the live-in set).");
            w.println("- `applicable_ops` (its only input, the byte budget, is already statements.byte_range_len).");
        } finally {
            w.close();
        }
        println("  wrote manifest      : " + mf.getAbsolutePath());
    }
}
