// CallEdgePass.java -- the CALL-GRAPH EDGE pass for the production reference-data
// extractor (parallel-ghidra-research.md §4b -- the call-graph BACKBONE of the
// whole discovery model: find{} walks UP caller edges from an anchor to surface
// gameplay functions).
//
// ONE CONCERN: iterate every function and, for each CALL instruction in its body,
// emit one edge row into the RVA-sharded `call_edges/` table. No decompile (the
// graph comes from the disassembly listing + call references -- zero decompile
// cost). Its own pass class.
//
// call_edges/ COLUMNS: caller_rva, callee_rva, callsite_rva, module, game_version,
//   edge_reason
//   - One directed edge per CALLSITE (callsite_rva is load-bearing for the
//     mode=callsite locator + anchor precision). The import indexes both
//     directions (caller-index = the shard grouping; callee-index = sort/groupby
//     on callee_rva). Shard key = CALLER rva (a function's outbound edges
//     co-shard with its functions/+statements/ rows).
//   - callee resolution: direct -> function entry rva; thunk -> resolved THROUGH
//     getThunkedFunction; into-body -> containing function entry; out-of-module
//     import -> edge_reason=external, EMPTY callee_rva (the inModuleImage guard --
//     getOffset()-imageBase would UNDERFLOW to garbage; AP14); non-function
//     target -> edge_reason=unresolved_target, empty; indirect (call rax / call
//     [rip+x], no call reference) -> edge_reason=indirect, empty callee_rva.
//   - The mechanism: getReferencesFrom() filtered by ReferenceType.isCall() (the
//     proven SizeExtractionCost mechanism, 93% reliable per the anchor probe).
//
// RVA-RANGE FILTER: a worker owns the edges whose CALLER is in its [start,end).
// Filter on caller_rva ONLY -- a callee may be out-of-range, the edge still
// emits with its real callee_rva (the merge is keyed on caller_rva via the shard,
// so no edge is double-emitted or dropped across disjoint workers). Heartbeat
// every 10000 functions.
//
// AP14: every CALL emits a visible row (indirect/external/unresolved are VISIBLE
// counted rows -- exactly where the static graph is blind and find{} must know,
// never silent drops). A leaf/call-free function is normal (counted). An
// xref-enumeration failure is logged loud + counted. Accounting:
// resolved+indirect+external+unresolved == emitted.

package refdata;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;

public final class CallEdgePass {

    public static final String HEADER =
        "caller_rva,callee_rva,callsite_rva,module,game_version,edge_reason";

    public static final class Stats {
        public long functionsSeen = 0;
        public long edgesEmitted = 0;
        public long resolvedDirect = 0;
        public long thunkResolved = 0;
        public long intoBodyResolved = 0;
        public long indirect = 0;
        public long external = 0;
        public long unresolvedTarget = 0;
        public long functionsWithNoCallees = 0;
        public long xrefEnumerationFailures = 0;
    }

    private final Program program;
    private final Listing listing;
    private final long imageBase;
    private final Address imageBaseAddr;
    private final String module;
    private final String versionTag;
    private final ShardWriter edgesOut;
    private final RvaRange range;
    private final GhidraScript script;

    public CallEdgePass(GhidraScript script, ShardWriter edgesOut,
                        String versionTag, RvaRange range) {
        this.script = script;
        this.program = script.getCurrentProgram();
        this.listing = program.getListing();
        this.imageBaseAddr = program.getImageBase();
        this.imageBase = imageBaseAddr.getOffset();
        this.module = program.getName();
        this.versionTag = versionTag;
        this.edgesOut = edgesOut;
        this.range = range;
    }

    public Stats run(int limit) throws Exception {
        Stats s = new Stats();
        FunctionIterator fns = listing.getFunctions(true);
        while (fns.hasNext()) {
            if (script.getMonitor().isCancelled()) break;
            if (limit >= 0 && s.functionsSeen >= limit) break;
            Function fn = fns.next();
            long callerRva = fn.getEntryPoint().getOffset() - imageBase;
            if (!range.contains(callerRva)) {
                continue; // worker owns edges whose CALLER is in range; not counted.
            }
            emitFunctionEdges(fn, callerRva, s);
            if (s.functionsSeen % 10000 == 0) {
                script.println(String.format(
                    "[ProduceReferenceData] CallEdgePass: processed %d functions...",
                    s.functionsSeen));
            }
        }
        return s;
    }

    private void emitFunctionEdges(Function fn, long callerRva, Stats s) {
        s.functionsSeen++;
        long edgesForThisFn = 0;
        try {
            for (Instruction ins : listing.getInstructions(fn.getBody(), true)) {
                if (ins.getFlowType().isCall()) {
                    edgesForThisFn += emitCallsiteEdges(ins, callerRva, s);
                }
            }
        } catch (Exception e) {
            // An xref/instruction-walk failure is an edge state that MUST be
            // visible (AP14): the function's edges are unknowable here -- surface
            // it loudly + count it, never let the function silently vanish.
            s.xrefEnumerationFailures++;
            script.println(String.format(
                "  [CallEdgePass] WARNING: xref enumeration FAILED for FUN_%08x (%s) "
                + "-- its call edges are unknown; counted, not dropped",
                fn.getEntryPoint().getOffset(), e.getClass().getSimpleName()));
            return;
        }
        if (edgesForThisFn == 0) {
            // A leaf / call-free function. NORMAL -- but counted so every function
            // is accounted for (find{} treats a no-callee fn as a graph sink).
            s.functionsWithNoCallees++;
        }
    }

    /**
     * Emit the edge row(s) for ONE call instruction. A direct call has one call
     * reference; some sites carry multiple (computed targets) -- one row per
     * reference. A call instruction with NO call reference is indirect -> one
     * row, callee_rva empty, edge_reason=indirect. Returns the rows written.
     */
    private long emitCallsiteEdges(Instruction ins, long callerRva, Stats s) {
        long callsiteRva = ins.getAddress().getOffset() - imageBase;
        long written = 0;
        Reference[] refs = ins.getReferencesFrom();
        for (Reference r : refs) {
            RefType rt = (RefType) r.getReferenceType();
            if (rt == null || !rt.isCall()) continue;
            Address target = r.getToAddress();
            String calleeRva = "";
            String edgeReason = "";

            Function callee = resolveCalleeFunction(target);
            if (callee != null) {
                Function thunked = callee.getThunkedFunction(true);
                boolean isThunk = false;
                boolean intoBody = false;
                if (thunked != null) {
                    callee = thunked; // resolve THROUGH the thunk to the real target.
                    isThunk = true;
                } else if (!callee.getEntryPoint().equals(target)) {
                    intoBody = true; // call landed inside a body, resolved to entry.
                }
                Address calleeEntry = callee.getEntryPoint();
                if (!inModuleImage(calleeEntry)) {
                    // External import: a real call OUT of the module, but no
                    // in-module rva to join on (getOffset()-imageBase would
                    // UNDERFLOW to garbage -- the AP14 bug). Mark external + empty.
                    edgeReason = "external";
                    s.external++;
                } else {
                    long cr = calleeEntry.getOffset() - imageBase;
                    calleeRva = "0x" + Long.toHexString(cr);
                    s.resolvedDirect++;
                    if (isThunk) {
                        edgeReason = "thunk";
                        s.thunkResolved++;
                    } else if (intoBody) {
                        edgeReason = "into_body";
                        s.intoBodyResolved++;
                    }
                }
            } else {
                // A call reference to a non-function address. VISIBLE + counted.
                edgeReason = "unresolved_target";
                s.unresolvedTarget++;
            }

            writeEdge(callerRva, calleeRva, callsiteRva, edgeReason);
            s.edgesEmitted++;
            written++;
        }
        if (written == 0) {
            // No call reference on a call instruction = indirect (call rax /
            // call [rip+x]). VISIBLE counted row, empty callee_rva -- exactly
            // where the static graph is blind and find{} must know (AP14).
            writeEdge(callerRva, "", callsiteRva, "indirect");
            s.edgesEmitted++;
            s.indirect++;
            written++;
        }
        return written;
    }

    private Function resolveCalleeFunction(Address target) {
        if (target == null) return null;
        Function f = listing.getFunctionAt(target);
        if (f != null) return f;
        return listing.getFunctionContaining(target);
    }

    /**
     * IN this module's image (same address space as the image base AND at/above
     * the base). An external import lives in Ghidra's EXTERNAL address space (a
     * different space) or below the base -- either way it has no in-module RVA,
     * and getOffset()-imageBase would produce garbage. Callers use this to mark
     * such a callee `external` rather than emit a bad rva.
     */
    private boolean inModuleImage(Address a) {
        if (a == null) return false;
        if (!a.getAddressSpace().equals(imageBaseAddr.getAddressSpace())) return false;
        return a.getOffset() >= imageBase;
    }

    private void writeEdge(long callerRva, String calleeRva, long callsiteRva,
                           String edgeReason) {
        StringBuilder row = new StringBuilder();
        row.append(Csv.q("0x" + Long.toHexString(callerRva))).append(',')
           .append(Csv.q(calleeRva)).append(',')
           .append(Csv.q("0x" + Long.toHexString(callsiteRva))).append(',')
           .append(Csv.q(module)).append(',')
           .append(Csv.q(versionTag)).append(',')
           .append(Csv.q(edgeReason));
        edgesOut.writeRow(callerRva, row.toString());
    }
}
