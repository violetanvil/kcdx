// FunctionPass.java -- the PER-FUNCTION extraction pass for the production
// reference-data extractor (parallel-ghidra-research.md §4a; restructure-plan.md
// Phase 9.1 `functions` table).
//
// ONE CONCERN: iterate every function in the program, and for each emit ONE row
// into the RVA-sharded `functions/` table (via ShardWriter) carrying the
// mechanical per-function fields. This is the FIRST of several passes; the
// statement pass and call-edge pass are SEPARATE classes that slot alongside
// this one against the same ShardWriter + the same fn->shard mapping (no
// monolith run()).
//
// COLUMNS EMITTED (functions table):
//   module, game_version, rva, length, auto_name, function_name, namespace,
//   content_hash, signature, signature_source, decompile_quality, edge_reason
//
//   - module/game_version : stamped (program name / version arg).
//   - rva                 : entry - imageBase, "0x"+hex. The engine's seek offset.
//   - length              : getBody().getNumAddresses() == the [rva,rva+length)
//                           span hashed (BLAKE3-HASH-CONTRACT.md -- the
//                           contract-required column so the engine reproduces
//                           the hashed range).
//   - auto_name           : ALWAYS populated (real name if auto-named, else
//                           synth FUN_<va>).
//   - function_name       : EMPTY for auto-named fns (sparse curated overlay,
//                           §4c -- not dump-populated).
//   - namespace           : RTTI namespace if any, else empty.
//   - content_hash        : BLAKE3 of [rva,rva+length) on-disk bytes, lowercase
//                           hex (ContentHash). EMPTY when bytes unreadable --
//                           with edge_reason saying why (AP14).
//   - signature           : Ghidra's decompiler-inferred prototype THIS step.
//   - signature_source    : "ghidra" THIS step. The abi_walker signatures pass
//                           (produce_signatures.py) overwrites it at merge with
//                           "abi_walker".
//   - decompile_quality   : clean | partial | unanalyzable (mapping below).
//   - edge_reason         : EMPTY on the happy path; a short token naming WHY a
//                           function is degraded/unhashable when it is (AP14 --
//                           the edge state is VISIBLE per-row, never a silent skip).
//   subsystem / inferred_purpose are the sparse curated overlay (§4c) and are
//   NOT emitted by the mechanical dump. `id` / `status` are MAINTAINER-ASSIGNED
//   AT IMPORT (§5/§9), never dump-emitted.
//
// DECOMPILE_QUALITY MAPPING (documented here so the engine's `statement.*`
// quality gate has a defined contract):
//   clean        := dr != null && dr.decompileCompleted() && dr.getHighFunction() != null
//   partial      := dr != null && dr.decompileCompleted() == true BUT
//                   getHighFunction() == null  (decompiler ran but produced no
//                   high-function -- usable shell, no reliable statement IR;
//                   the engine gates statement.* OFF for these).
//   unanalyzable := dr == null || !dr.decompileCompleted() (timeout, exception,
//                   refusal) -- OR the bytes could not be read for the hash.
//
// RVA-RANGE FILTER (RvaRange): only functions whose rva is in the worker's
// [start,end) are processed; out-of-range functions are skipped ENTIRELY before
// any emit/tally (a skipped function is out-of-scope, NOT an edge state -- it is
// not counted). Default = full range = byte-identical to all-functions behavior.
//
// EDGE STATES MADE VISIBLE (AP14 -- a 321K batch WILL hit these; none silently
// dropped): bytes unreadable / short read / non-positive length -> content_hash
// EMPTY + decompile_quality=unanalyzable + edge_reason; decompile timeout/failure
// -> unanalyzable + edge_reason; decompiled-but-no-HighFunction -> partial +
// edge_reason. Every IN-RANGE function produces exactly one row; the summary's
// total MUST equal the in-range function count (the silent-skip check).

package refdata;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Namespace;

public final class FunctionPass {

    // Ghidra's universal auto-name prefixes.
    private static final String[] AUTO_PREFIXES = {
        "FUN_", "SUB_", "LAB_", "thunk_FUN_", "UndefinedFunction_",
        "DAT_", "EXT_", "switchD_", "caseD_", "j_"
    };

    private static boolean isAutoName(String name) {
        for (String p : AUTO_PREFIXES) {
            if (name.startsWith(p)) {
                return true;
            }
        }
        return false;
    }

    public static final String HEADER =
        "module,game_version,rva,length,auto_name,function_name,namespace,"
        + "content_hash,signature,signature_source,decompile_quality,edge_reason";

    // Per-function decompile budget (seconds).
    private static final int DECOMPILE_TIMEOUT_S = 60;

    /** Tallies the run summary reports. Total MUST equal the in-range count. */
    public static final class Stats {
        public long total = 0;
        public long clean = 0;
        public long partial = 0;
        public long unanalyzable = 0;
        public long bytesUnreadable = 0;
        public long autoNamed = 0;
        public long sourceNamed = 0;
    }

    private final Program program;
    private final long imageBase;
    private final String module;
    private final String versionTag;
    private final ShardWriter functionsOut;
    private final RvaRange range;
    private final GhidraScript script; // for monitor + println

    public FunctionPass(GhidraScript script, ShardWriter functionsOut,
                        String versionTag, RvaRange range) {
        this.script = script;
        this.program = script.getCurrentProgram();
        this.imageBase = program.getImageBase().getOffset();
        this.module = program.getName();
        this.versionTag = versionTag;
        this.functionsOut = functionsOut;
        this.range = range;
    }

    /**
     * Iterate functions and emit one row each. {@code limit < 0} = all functions
     * (the production default); {@code limit >= 0} caps the count WITHIN the
     * range for a quick sample run.
     */
    public Stats run(int limit) throws Exception {
        Stats s = new Stats();

        DecompInterface di = new DecompInterface();
        di.openProgram(program);
        try {
            FunctionIterator fns = program.getListing().getFunctions(true);
            while (fns.hasNext()) {
                if (script.getMonitor().isCancelled()) {
                    break;
                }
                if (limit >= 0 && s.total >= limit) {
                    break;
                }
                Function fn = fns.next();
                long rva = fn.getEntryPoint().getOffset() - imageBase;
                if (!range.contains(rva)) {
                    continue; // out-of-scope for this worker; NOT counted (AP14).
                }
                emitOne(di, fn, s);
                // Progress heartbeat (observability): every 10000 functions, emit
                // a wall-progress line so a watcher sees pace, not silence.
                if (s.total % 10000 == 0) {
                    script.println(String.format(
                        "[ProduceReferenceData] FunctionPass: processed %d functions...", s.total));
                }
            }
        } finally {
            di.dispose();
        }
        return s;
    }

    private void emitOne(DecompInterface di, Function fn, Stats s) {
        s.total++;

        Address entry = fn.getEntryPoint();
        long rva = entry.getOffset() - imageBase;
        long length = fn.getBody().getNumAddresses();

        String name = fn.getName();
        boolean autoNamed = isAutoName(name);
        if (autoNamed) {
            s.autoNamed++;
        } else {
            s.sourceNamed++;
        }
        // auto_name ALWAYS populated; function_name EMPTY when only auto-named
        // (sparse curated overlay -- §4c).
        String autoName = autoNamed ? name : String.format("FUN_%08x", entry.getOffset());
        String functionName = autoNamed ? "" : name;

        Namespace ns = fn.getParentNamespace();
        String namespace = (ns == null || ns.isGlobal()) ? "" : ns.getName(true);

        // --- decompile: drives quality + the Ghidra-inferred signature ---
        DecompileResults dr = null;
        String decompFailReason = null;
        try {
            dr = di.decompileFunction(fn, DECOMPILE_TIMEOUT_S, script.getMonitor());
        } catch (Exception e) {
            decompFailReason = "decompile_exception:" + e.getClass().getSimpleName();
        }
        boolean completed = dr != null && dr.decompileCompleted();
        boolean hasHigh = completed && dr.getHighFunction() != null;

        // --- content_hash: raw on-disk [rva, rva+length) (BLAKE3 contract) ---
        String contentHash = "";
        String hashFailReason = null;
        try {
            contentHash = ContentHash.ofRange(program.getMemory(), entry, length);
        } catch (MemoryAccessException e) {
            hashFailReason = "bytes_unreadable:" + e.getMessage();
            s.bytesUnreadable++;
        }

        // --- decompile_quality + edge_reason ---
        String quality;
        String edgeReason = "";
        if (hashFailReason != null) {
            quality = "unanalyzable";
            edgeReason = hashFailReason;
        } else if (hasHigh) {
            quality = "clean";
            s.clean++;
        } else if (completed) {
            quality = "partial";
            edgeReason = "no_high_function";
            s.partial++;
        } else {
            quality = "unanalyzable";
            edgeReason = (decompFailReason != null) ? decompFailReason : "decompile_failed";
            s.unanalyzable++;
        }
        // A hash-fail row set quality=unanalyzable above but was counted under
        // bytesUnreadable, not unanalyzable -- count it here too so the quality
        // breakdown balances (total == clean+partial+unanalyzable).
        if (hashFailReason != null) {
            s.unanalyzable++;
        }

        // --- signature: Ghidra's inferred prototype (this step) ---
        // (false,false) = formal params, no calling-convention prefix -- the bare
        // prototype text. produce_signatures.py overwrites it at merge with
        // signature_source=abi_walker.
        String signature = fn.getPrototypeString(false, false);
        String signatureSource = "ghidra";

        StringBuilder row = new StringBuilder();
        row.append(Csv.q(module)).append(',')
           .append(Csv.q(versionTag)).append(',')
           .append(Csv.q("0x" + Long.toHexString(rva))).append(',')
           .append(length).append(',')
           .append(Csv.q(autoName)).append(',')
           .append(Csv.q(functionName)).append(',')
           .append(Csv.q(namespace)).append(',')
           .append(Csv.q(contentHash)).append(',')
           .append(Csv.q(signature)).append(',')
           .append(Csv.q(signatureSource)).append(',')
           .append(Csv.q(quality)).append(',')
           .append(Csv.q(edgeReason));

        functionsOut.writeRow(rva, row.toString());
    }
}
