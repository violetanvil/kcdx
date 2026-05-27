// FindLocManager.java -- locate the localization-manager structure in WHGame.dll
// for the loc int-ID resolution sub-feature (parallel-ghidra-research.md §6 step 2).
//
// Strategy: the loc-manager methods are the functions that reference the
// localization string anchors (CLocalizedStringsManager, LocalizeString,
// LoadLocalizationXml, LocalizedStringManager.cpp). Find those strings, find
// the functions that reference them, and report each with its decompiled body
// so we can read (a) how the manager is reached and (b) the key->int-ID table
// layout it walks.
//
// Read-only. Run:
//   analyzeHeadless <proj_dir> KCD2 -process WHGame.dll \
//       -postScript FindLocManager.java -noanalysis -readOnly
//
//@category Research

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class FindLocManager extends GhidraScript {

    // The string anchors that mark loc-manager code. Substring match.
    private static final String[] ANCHORS = {
        "CLocalizedStringsManager", "LocalizedStringManager.cpp",
        "LoadLocalizationXml", "LocalizeString", "LoadLocalizationDataByTag"
    };

    private static boolean isAnchor(String s) {
        for (String a : ANCHORS) {
            if (s.contains(a)) return true;
        }
        return false;
    }

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Find the anchor string literals + the functions referencing them.
        Set<Function> locFns = new HashSet<>();
        List<String> anchorHits = new ArrayList<>();
        DataIterator strs = currentProgram.getListing().getDefinedData(true);
        for (Data d : strs) {
            if (monitor.isCancelled()) break;
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (!isAnchor(s)) continue;
            Address strAddr = d.getAddress();
            // who references this string?
            List<String> refFns = new ArrayList<>();
            for (Reference r : rm.getReferencesTo(strAddr)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) {
                    locFns.add(f);
                    refFns.add(f.getName() + "@" + f.getEntryPoint());
                }
            }
            anchorHits.add(String.format("  @%s  %-45s  refd-by=%s",
                strAddr, ("\"" + (s.length() > 42 ? s.substring(0, 42) + "…" : s) + "\""),
                refFns.isEmpty() ? "(none)" : String.join(", ", refFns)));
        }

        println("=== anchor strings + referencing functions ===");
        for (String h : anchorHits) println(h);
        println("\n=== " + locFns.size() + " candidate loc-manager functions ===");

        // 2. Decompile each candidate -- read structure access (this->field, table
        //    walks, key->id lookups). Cap output per function.
        for (Function f : locFns) {
            if (monitor.isCancelled()) break;
            println("\n" + "-".repeat(72));
            println("FUNC " + f.getName() + " @ " + f.getEntryPoint()
                    + "  size=" + f.getBody().getNumAddresses());
            DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted()) {
                String c = dr.getDecompiledFunction().getC();
                // Cap to keep the log readable; the prologue + struct accesses
                // are what we need for the layout.
                String[] lines = c.split("\n");
                int limit = Math.min(lines.length, 80);
                for (int i = 0; i < limit; i++) println("  " + lines[i]);
                if (lines.length > limit) println("  … (" + (lines.length - limit) + " more lines)");
            } else {
                println("  <decompile failed>");
            }
        }

        di.dispose();
        println("\n=== done ===");
    }
}
