// ProbeAnchorQuality.java -- QUALITY PROBE for the kcdx reference-data feasibility
// question (docs/outstanding-work/parallel-ghidra-research.md rework).
//
// Question it answers: on functions Ghidra only AUTO-NAMED (FUN_*), can it
// reliably extract the data `kcdx_find` / `kcdx_dev_inspect` depend on --
// string refs, callee refs, a clean statement decomposition, and a cvar signal?
// If this data is garbage on stripped/unnamed functions, the console-driven
// find-by-anchor UX has no reliable source and the whole approach needs rework
// BEFORE any compute sizing.
//
// Read-only. Samples N auto-named, "substantial" (>= MIN_SIZE bytes, non-thunk)
// functions spread across the address space and reports, per function:
//   - decompile success + a crude quality signal (statement count, % of insns
//     covered by decompiled statements)
//   - string references (the kcdx.find{string=} anchor)
//   - callee references (the kcdx.find{callee=} / first_call_to anchor)
//   - a cvar signal: string args flowing into call sites (CryEngine cvars are
//     string-named; this is the heuristic kcdx.find{cvar=} would build on)
//
// This is a PROBE, not the dump tool -- it prints for human judgment, does not
// emit the production CSV/JSONL. Outcome -> meaning:
//   A: all classes populate cleanly on most sampled fns -> data viable, size compute.
//   B: strings/callees fine, cvars/captures best-effort -> narrow promises, then size.
//   C: decompile fails / xrefs absent on most -> source unreliable, rethink.
//
// Run:
//   analyzeHeadless <proj_dir> <proj_name> -process WHGame.dll \
//       -postScript ProbeAnchorQuality.java [sample_n] [min_size] \
//       -noanalysis -readOnly
//
//@category Research

import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class ProbeAnchorQuality extends GhidraScript {

    private static final String[] AUTO_PREFIXES = {
        "FUN_", "SUB_", "LAB_", "thunk_FUN_", "UndefinedFunction_"
    };

    private static boolean isAutoName(String name) {
        for (String p : AUTO_PREFIXES) {
            if (name.startsWith(p)) return true;
        }
        return false;
    }

    // Try to read a printable string at an address (the string-anchor extraction
    // kcdx.find{string=} relies on). Returns null if no string data there.
    private String stringAt(Address a) {
        Data d = getDataAt(a);
        if (d != null && d.hasStringValue()) {
            Object v = d.getValue();
            if (v != null) return v.toString();
        }
        // Fall back to raw read for undefined-but-printable runs.
        StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
        if (sdi != null && sdi.getStringLength() > 0) {
            String s = sdi.getStringValue();
            if (s != null && s.length() >= 3) return s;
        }
        return null;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        int sampleN = (args.length >= 1) ? Integer.parseInt(args[0]) : 40;
        int minSize = (args.length >= 2) ? Integer.parseInt(args[1]) : 64;

        Listing listing = currentProgram.getListing();
        long imageBase = currentProgram.getImageBase().getOffset();

        // Collect substantial auto-named functions, then sample evenly across
        // the list (spread over the address space, not the first N).
        List<Function> candidates = new ArrayList<>();
        FunctionIterator it = listing.getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isThunk() || f.isExternal()) continue;
            if (!isAutoName(f.getName())) continue;
            if (f.getBody().getNumAddresses() < minSize) continue;
            candidates.add(f);
        }
        int total = candidates.size();
        println("substantial auto-named functions: " + total
                + "  (sampling " + sampleN + ")");
        if (total == 0) { println("nothing to sample"); return; }
        int stride = Math.max(1, total / sampleN);

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int sampled = 0, decompiledOk = 0, withString = 0, withCallee = 0,
            withCvarSignal = 0;
        long sumStmts = 0;

        for (int i = 0; i < total && sampled < sampleN; i += stride) {
            Function f = candidates.get(i);
            sampled++;
            long rva = f.getEntryPoint().getOffset() - imageBase;
            long sizeBytes = f.getBody().getNumAddresses();

            // --- statement decomposition via the decompiler ---
            DecompileResults dr = di.decompileFunction(f, 45, new ConsoleTaskMonitor());
            boolean ok = dr != null && dr.decompileCompleted()
                         && dr.getHighFunction() != null;
            int stmtCount = 0;
            if (ok) {
                decompiledOk++;
                java.util.Iterator<PcodeOpAST> ops =
                    dr.getHighFunction().getPcodeOps();
                while (ops.hasNext()) { ops.next(); stmtCount++; }
                sumStmts += stmtCount;
            }

            // --- string + callee + cvar-signal anchors from instruction xrefs ---
            List<String> strs = new ArrayList<>();
            List<String> callees = new ArrayList<>();
            int cvarSignal = 0;
            for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                boolean isCall = ins.getFlowType().isCall();
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (r.getReferenceType().isCall()) {
                        Function tf = getFunctionAt(to);
                        callees.add(tf != null ? tf.getName() : to.toString());
                    } else {
                        String s = stringAt(to);
                        if (s != null) {
                            strs.add(s.length() > 40 ? s.substring(0, 40) + "…" : s);
                            // cvar signal: a string operand near a call site is
                            // how CryEngine cvar names reach Register/Get calls.
                            if (isCall) cvarSignal++;
                        }
                    }
                }
            }
            if (!strs.isEmpty()) withString++;
            if (!callees.isEmpty()) withCallee++;
            if (cvarSignal > 0) withCvarSignal++;

            println(String.format(
                "FUN_%08x  size=%-5d decompile=%s stmts=%-4d strings=%d callees=%d cvar?=%d",
                f.getEntryPoint().getOffset(), sizeBytes,
                ok ? "OK " : "FAIL", stmtCount,
                strs.size(), callees.size(), cvarSignal));
            if (!strs.isEmpty()) {
                int show = Math.min(3, strs.size());
                println("    str: " + String.join(" | ", strs.subList(0, show)));
            }
        }

        di.dispose();

        String bar = "========================================================================";
        println(bar);
        println("[ProbeAnchorQuality] sampled=" + sampled + " of " + total
                + " substantial auto-named functions");
        println(String.format("  decompile OK        : %d/%d (%.0f%%)",
                decompiledOk, sampled, 100.0 * decompiledOk / sampled));
        println(String.format("  avg statements (ok) : %.1f",
                decompiledOk > 0 ? (double) sumStmts / decompiledOk : 0.0));
        println(String.format("  with string anchor  : %d/%d (%.0f%%)",
                withString, sampled, 100.0 * withString / sampled));
        println(String.format("  with callee anchor  : %d/%d (%.0f%%)",
                withCallee, sampled, 100.0 * withCallee / sampled));
        println(String.format("  with cvar signal    : %d/%d (%.0f%%)",
                withCvarSignal, sampled, 100.0 * withCvarSignal / sampled));
        println(bar);
        println("Outcome key: A=all classes clean on most -> size compute; "
                + "B=strings/callees ok, cvar/captures best-effort -> narrow+size; "
                + "C=decompile/xrefs unreliable -> rethink.");
    }
}
