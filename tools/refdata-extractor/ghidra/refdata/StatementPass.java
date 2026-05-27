// StatementPass.java -- the PER-STATEMENT + ANCHOR pass for the production
// reference-data extractor (parallel-ghidra-research.md §4a; restructure-plan.md
// Phase 9.1 `statements` table).
//
// ONE CONCERN: for each CLEAN function (a HighFunction + usable Clang markup),
// enumerate its statements and emit, per statement, one row into the RVA-sharded
// `statements/` table, plus the per-statement `referenced_vars/` child table.
// Its own pass class; slots alongside FunctionPass/CallEdgePass against the same
// ShardWriter + fn->shard mapping. No bloat of FunctionPass.
//
// STATEMENT SOURCE: the decompiler's Clang markup (getCCodeMarkup() ->
// ClangStatement nodes), NOT the raw HighFunction pcode-op stream -- §4a needs
// the rendered statement text + a real machine-code range, which the flat pcode
// list lacks. NOTE: ClangTokenGroup.flatten() collects LEAF tokens only (a group
// never adds itself), so we walk numChildren()/Child(i) recursively and collect
// ClangStatement nodes directly (probed: the flatten() approach emitted 0
// statements; the recursive walk emits them correctly).
//
// statements/ COLUMNS:
//   function_rva, idx, kind, pseudo_text, byte_range_start, byte_range_len,
//   content_hash, callee, callee_rva, string_ref, cvar_ref, edge_reason
//   - function_rva + idx : the (function_rva, idx) JOIN KEY (idx 0-based, dense:
//     a no-machine-code statement does NOT advance idx, so emitted rows stay
//     contiguous). The import joins statements->functions + child tables->statements.
//   - kind : mechanical pcode-root taxonomy (call/return/branch/store/assign/
//     other/none) -- a category, not a gameplay judgment.
//   - pseudo_text : the decompiler's rendered statement text.
//   - byte_range_start : RVA of the statement's first machine instruction
//     (minAddr - imageBase) -- a real address, NOT a pcode index.
//   - byte_range_len : (maxInsnAddr - minInsnAddr) + len(insn at maxInsnAddr).
//     The +len is load-bearing (getMaxAddress is the START of the last insn, so
//     a single-insn statement would be len 0 without it). The fit-or-trampoline
//     budget AND the span the per-statement BLAKE3 hash covers.
//   - content_hash : BLAKE3 of the statement's on-disk byte sub-range
//     [byte_range_start, +byte_range_len) -- the SAME contract, scoped.
//   - callee (+ callee_rva) : if a call -- the callee's name + in-module rva.
//     callee_rva is EMPTY for an out-of-module (EXTERNAL-space) callee (the
//     inModuleImage guard -- getOffset()-imageBase would UNDERFLOW to garbage;
//     AP14: the name/anchor stays, the rva empties, never the garbage value).
//   - string_ref : if the statement references a string literal.
//   - cvar_ref : EMPTY (§4a redundant-with-string; no cheap non-redundant source).
//
// referenced_vars/ CHILD TABLE (function_rva, statement_idx, var_name,
//   storage_kind, storage_detail, size_bytes, data_type): the per-statement
//   REFERENCED-variable storage set Ghidra provides. It is an APPROXIMATION of
//   §4a's "live registers/stack" captures field (it includes const/unique
//   varnodes that are not live storage), NOT the live-in set. Named honestly --
//   the TRUE live-in `captures` table is DEFERRED to Phase 9.3 (which owns the
//   engine's mid-hook marshalling contract + will derive the live-in set, likely
//   from these same primitives + a liveness pass).
//
// applicable_ops table NOT emitted: its only currently-available input (the byte
//   budget) is already carried as statements.byte_range_len -- a separate table
//   would be fully redundant. DEFERRED to Phase 9.3 (which joins the budget
//   against the engine's kcdx.op.* catalog).
//
// RVA-RANGE FILTER + heartbeat: same as FunctionPass -- out-of-range functions
// skipped before decompile (the speed win), not counted; heartbeat every 10000.
//
// AP14: clean function with statements; partial/unanalyzable -> NO statements
// (counted, the honest gate); a statement with no machine address -> skipped,
// idx NOT advanced, counted; statement bytes unreadable -> row emitted with empty
// content_hash + edge_reason. Accounting: clean+partial+unanalyzable+decompileFailed
// == functionsSeen (in-range), loud WARNING on mismatch.

package refdata;

import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.ClangFuncNameToken;
import ghidra.app.decompiler.ClangNode;
import ghidra.app.decompiler.ClangStatement;
import ghidra.app.decompiler.ClangToken;
import ghidra.app.decompiler.ClangTokenGroup;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;

public final class StatementPass {

    public static final String STATEMENTS_HEADER =
        "function_rva,idx,kind,pseudo_text,byte_range_start,byte_range_len,"
        + "content_hash,callee,callee_rva,string_ref,cvar_ref,edge_reason";

    public static final String REFERENCED_VARS_HEADER =
        "function_rva,statement_idx,var_name,storage_kind,storage_detail,size_bytes,data_type";

    private static final int DECOMPILE_TIMEOUT_S = 60;

    public static final class Stats {
        public long functionsSeen = 0;
        public long cleanFunctions = 0;
        public long partialSkipped = 0;
        public long unanalyzableSkipped = 0;
        public long decompileFailed = 0;
        public long statementsEmitted = 0;
        public long noAddrSkipped = 0;
        public long calleeAnchors = 0;
        public long stringAnchors = 0;
        public long referencedVarsEmitted = 0;
    }

    private final Program program;
    private final Listing listing;
    private final long imageBase;
    private final Address imageBaseAddr;
    private final ShardWriter statementsOut;
    private final ShardWriter referencedVarsOut;
    private final RvaRange range;
    private final GhidraScript script;

    public StatementPass(GhidraScript script, ShardWriter statementsOut,
                         ShardWriter referencedVarsOut, RvaRange range) {
        this.script = script;
        this.program = script.getCurrentProgram();
        this.listing = program.getListing();
        this.imageBaseAddr = program.getImageBase();
        this.imageBase = imageBaseAddr.getOffset();
        this.statementsOut = statementsOut;
        this.referencedVarsOut = referencedVarsOut;
        this.range = range;
    }

    public Stats run(int limit) throws Exception {
        Stats s = new Stats();
        DecompInterface di = new DecompInterface();
        di.openProgram(program);
        try {
            FunctionIterator fns = listing.getFunctions(true);
            while (fns.hasNext()) {
                if (script.getMonitor().isCancelled()) break;
                if (limit >= 0 && s.functionsSeen >= limit) break;
                Function fn = fns.next();
                long rva = fn.getEntryPoint().getOffset() - imageBase;
                if (!range.contains(rva)) {
                    continue; // out-of-scope; not counted (AP14).
                }
                emitFunction(di, fn, s);
                if (s.functionsSeen % 10000 == 0) {
                    script.println(String.format(
                        "[ProduceReferenceData] StatementPass: processed %d functions...",
                        s.functionsSeen));
                }
            }
        } finally {
            di.dispose();
        }
        return s;
    }

    private void emitFunction(DecompInterface di, Function fn, Stats s) {
        s.functionsSeen++;
        long fnRva = fn.getEntryPoint().getOffset() - imageBase;

        DecompileResults dr;
        try {
            dr = di.decompileFunction(fn, DECOMPILE_TIMEOUT_S, script.getMonitor());
        } catch (Exception e) {
            s.decompileFailed++;
            script.println(String.format(
                "  [StatementPass] decompile threw for FUN_%08x (%s) -- no statements; counted",
                fn.getEntryPoint().getOffset(), e.getClass().getSimpleName()));
            return;
        }
        boolean completed = dr != null && dr.decompileCompleted();
        HighFunction hf = completed ? dr.getHighFunction() : null;

        if (!completed) {
            s.unanalyzableSkipped++;
            return;
        }
        if (hf == null) {
            s.partialSkipped++; // == FunctionPass's `partial`.
            return;
        }
        ClangTokenGroup markup = dr.getCCodeMarkup();
        if (markup == null) {
            // A clean HighFunction with NULL C markup is anomalous -- surface it
            // loudly + count it as a decompile edge, never silently yield zero.
            s.decompileFailed++;
            script.println(String.format(
                "  [StatementPass] WARNING: FUN_%08x has a HighFunction but NULL C markup "
                + "-- no statements emitted; counted as a decompile edge",
                fn.getEntryPoint().getOffset()));
            return;
        }
        // Genuinely clean AND has usable Clang markup -> the ONLY path that
        // counts cleanFunctions (one bucket per function; disjoint from the
        // partial/unanalyzable/decompile-edge buckets above). [The null-markup
        // double-count bug fixed: cleanFunctions++ is here, AFTER the markup check.]
        s.cleanFunctions++;

        // Collect ClangStatement nodes in document order (recursive child walk;
        // flatten() collects leaf tokens only and would yield 0 statements).
        List<ClangStatement> stmts = new ArrayList<>();
        collectStatements(markup, stmts);

        int idx = 0;
        for (ClangStatement stmt : stmts) {
            // Machine-code range of the statement.
            Address minA = stmt.getMinAddress();
            Address maxA = stmt.getMaxAddress();
            if (minA == null || maxA == null) {
                s.noAddrSkipped++; // no machine code; idx NOT advanced (dense keys).
                continue;
            }
            long startRva = minA.getOffset() - imageBase;
            long len = machineCodeLen(minA, maxA);

            String kind = classifyKind(stmt);
            String pseudo = stmt.toString();

            String contentHash = "";
            String edgeReason = "";
            try {
                contentHash = ContentHash.ofRange(program.getMemory(), minA, len);
            } catch (MemoryAccessException e) {
                edgeReason = "bytes_unreadable:" + e.getMessage();
            }

            // Anchors + referenced vars from the statement's tokens.
            String callee = "";
            String calleeRva = "";
            String stringRef = "";
            List<ClangToken> tokens = new ArrayList<>();
            for (java.util.Iterator<ClangToken> ti = stmt.tokenIterator(true); ti.hasNext(); ) {
                tokens.add(ti.next());
            }
            for (ClangToken tok : tokens) {
                if (callee.isEmpty() && tok instanceof ClangFuncNameToken) {
                    Function callTarget = resolveCallee((ClangFuncNameToken) tok, stmt);
                    if (callTarget != null) {
                        // Out-of-module callee: keep the name + anchor, EMPTY rva
                        // (getOffset()-imageBase would underflow -- AP14).
                        callee = callTarget.getName();
                        Address calleeEntry = callTarget.getEntryPoint();
                        if (inModuleImage(calleeEntry)) {
                            long cr = calleeEntry.getOffset() - imageBase;
                            calleeRva = "0x" + Long.toHexString(cr);
                        }
                    }
                }
                if (stringRef.isEmpty()) {
                    String lit = stringLiteralFor(tok);
                    if (lit != null) {
                        stringRef = lit;
                    }
                }
            }
            if (!callee.isEmpty()) s.calleeAnchors++;
            if (!stringRef.isEmpty()) s.stringAnchors++;

            // cvar_ref: §4a redundant-with-string; no cheap source -> EMPTY.
            String cvarRef = "";

            StringBuilder row = new StringBuilder();
            row.append(Csv.q("0x" + Long.toHexString(fnRva))).append(',')
               .append(idx).append(',')
               .append(Csv.q(kind)).append(',')
               .append(Csv.q(pseudo)).append(',')
               .append(Csv.q("0x" + Long.toHexString(startRva))).append(',')
               .append(len).append(',')
               .append(Csv.q(contentHash)).append(',')
               .append(Csv.q(callee)).append(',')
               .append(Csv.q(calleeRva)).append(',')
               .append(Csv.q(stringRef)).append(',')
               .append(Csv.q(cvarRef)).append(',')
               .append(Csv.q(edgeReason));
            statementsOut.writeRow(fnRva, row.toString());
            s.statementsEmitted++;

            emitReferencedVars(fnRva, idx, stmt, s);
            idx++;
        }
    }

    /** Recursively collect ClangStatement nodes (NOT flatten -- leaf-only). */
    private void collectStatements(ClangNode node, List<ClangStatement> out) {
        if (node instanceof ClangStatement) {
            out.add((ClangStatement) node);
        }
        for (int i = 0; i < node.numChildren(); i++) {
            collectStatements(node.Child(i), out);
        }
    }

    /** byte_range_len = (maxStart - minStart) + len(insn at maxStart). */
    private long machineCodeLen(Address minA, Address maxA) {
        long span = maxA.getOffset() - minA.getOffset();
        Instruction last = listing.getInstructionAt(maxA);
        int lastLen = (last != null) ? last.getLength() : 1;
        return span + lastLen;
    }

    /** Mechanical pcode-root kind: call/return/branch/store/assign/other/none. */
    private String classifyKind(ClangStatement stmt) {
        PcodeOp root = stmt.getPcodeOp();
        if (root == null) return "none";
        switch (root.getOpcode()) {
            case PcodeOp.CALL:
            case PcodeOp.CALLIND:
            case PcodeOp.CALLOTHER:
                return "call";
            case PcodeOp.RETURN:
                return "return";
            case PcodeOp.BRANCH:
            case PcodeOp.CBRANCH:
            case PcodeOp.BRANCHIND:
                return "branch";
            case PcodeOp.STORE:
                return "store";
            case PcodeOp.COPY:
            case PcodeOp.LOAD:
            case PcodeOp.CAST:
            case PcodeOp.MULTIEQUAL:
            case PcodeOp.INDIRECT:
            case PcodeOp.SUBPIECE:
                return "assign";
            default:
                // INT_*/FLOAT_*/BOOL_*/PTR* and the rest collapse to assign vs other.
                int oc = root.getOpcode();
                if ((oc >= PcodeOp.INT_EQUAL && oc <= PcodeOp.FLOAT_ROUND)
                        || (oc >= PcodeOp.PTRADD && oc <= PcodeOp.PTRSUB)) {
                    return "assign";
                }
                return "other";
        }
    }

    /** Resolve a ClangFuncNameToken to the called Function, via its pcode target. */
    private Function resolveCallee(ClangFuncNameToken tok, ClangStatement stmt) {
        PcodeOp root = stmt.getPcodeOp();
        if (root == null) return null;
        int oc = root.getOpcode();
        if (oc != PcodeOp.CALL && oc != PcodeOp.CALLIND && oc != PcodeOp.CALLOTHER) {
            return null;
        }
        Varnode target = (root.getNumInputs() > 0) ? root.getInput(0) : null;
        if (target == null || !target.isAddress()) return null;
        return listing.getFunctionContaining(target.getAddress());
    }

    /** A string literal referenced by this token's address, if any. */
    private String stringLiteralFor(ClangToken tok) {
        if (tok.getMinAddress() == null) return null;
        for (Reference r : program.getReferenceManager()
                .getReferencesFrom(tok.getMinAddress())) {
            if (r.getReferenceType().isData()) {
                Data d = listing.getDataAt(r.getToAddress());
                if (d != null && d.hasStringValue()) {
                    Object v = d.getValue();
                    if (v != null) return v.toString();
                }
            }
        }
        return null;
    }

    /**
     * Emit the per-statement referenced-variable storage set (NOT the live-in
     * `captures` set -- that is the Phase-9.3 derivation; this is an honest
     * approximation Ghidra provides).
     */
    private void emitReferencedVars(long fnRva, int idx, ClangStatement stmt, Stats s) {
        java.util.Set<HighVariable> seen = new java.util.HashSet<>();
        for (java.util.Iterator<ClangToken> ti = stmt.tokenIterator(true); ti.hasNext(); ) {
            ClangToken tok = ti.next();
            HighVariable hv = (tok instanceof ghidra.app.decompiler.ClangVariableToken)
                ? ((ghidra.app.decompiler.ClangVariableToken) tok).getHighVariable()
                : null;
            if (hv == null || !seen.add(hv)) continue;
            Varnode rep = hv.getRepresentative();
            if (rep == null) continue;

            String varName = hv.getName() == null ? "" : hv.getName();
            String storageKind;
            String storageDetail = "";
            long size = rep.getSize();
            String dataType = hv.getDataType() == null ? "" : hv.getDataType().getName();

            if (rep.isRegister()) {
                storageKind = "register";
                ghidra.program.model.lang.Register reg =
                    program.getRegister(rep.getAddress(), rep.getSize());
                storageDetail = (reg != null) ? reg.getName() : "";
            } else if (rep.getAddress() != null && rep.getAddress().isStackAddress()) {
                storageKind = "stack";
                storageDetail = "stack[" + rep.getAddress().getOffset() + "]";
            } else if (rep.isConstant()) {
                storageKind = "const";
            } else if (rep.isUnique()) {
                storageKind = "unique";
            } else if (rep.getAddress() != null && inModuleImage(rep.getAddress())) {
                storageKind = "memory";
                storageDetail = "0x" + Long.toHexString(rep.getAddress().getOffset() - imageBase);
            } else {
                storageKind = "other";
            }

            StringBuilder row = new StringBuilder();
            row.append(Csv.q("0x" + Long.toHexString(fnRva))).append(',')
               .append(idx).append(',')
               .append(Csv.q(varName)).append(',')
               .append(Csv.q(storageKind)).append(',')
               .append(Csv.q(storageDetail)).append(',')
               .append(size).append(',')
               .append(Csv.q(dataType));
            referencedVarsOut.writeRow(fnRva, row.toString());
            s.referencedVarsEmitted++;
        }
    }

    /** IN this module's image (same address space + at/above the base). */
    private boolean inModuleImage(Address a) {
        if (a == null) return false;
        if (!a.getAddressSpace().equals(imageBaseAddr.getAddressSpace())) return false;
        return a.getOffset() >= imageBase;
    }
}
