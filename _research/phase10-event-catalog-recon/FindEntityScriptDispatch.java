// FindEntityScriptDispatch.java -- locate the CryEngine entity-script DISPATCH
// path in WHGame.dll for Phase 10 pilot item_picked_up (OnPickup).
//
// Question (theory-INDEPENDENT, ground-truth first): what engine function invokes
// a named callback on an entity's Lua script table (the Entity:CallScriptFunction
// / CScriptBind_Entity dispatch that would carry OnPickup), and is there a SINGLE
// central dispatch point (globally subscribable once) or per-entity-script-table
// dispatch (must wrap per entity)?
//
// Method: find the dispatch string anchors front-2 observed, list their
// referencing functions, decompile each, and ALSO walk the CScriptBind_Entity
// vftable / RTTI to find CallScriptFunction-shaped members. READ bodies; never
// infer a call edge.
//
// Read-only. Run:
//   analyzeHeadless <proj_dir> KCD2 -process WHGame.dll \
//       -scriptPath <scripts> -postScript FindEntityScriptDispatch.java -noanalysis -readOnly
//
//@category Research

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
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
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class FindEntityScriptDispatch extends GhidraScript {

    // Dispatch + anchor string literals to locate (exact match).
    private static final String[] ANCHORS = {
        "Entity:CallScriptFunction",
        "CallScriptFunction",
        "CallScriptFunctionInTable",
        "CallScriptFunctionWithParam",
        "CallStateFunction",
        "ActivateOutput called with undefined event %s for entity %s",
        "OnPickup",
        "OnUse",
        "OnUsed",
        "ScriptBind_Entity",
        "CryEntitySystem\\ScriptBind_Entity.cpp",
        "ProcessEvent"
    };

    private static boolean isAnchor(String s) {
        for (String a : ANCHORS) if (s.equals(a)) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Map<String, List<Function>> byName = new LinkedHashMap<>();
        Map<String, Address> strAddrByName = new LinkedHashMap<>();
        Set<Function> allRefFns = new HashSet<>();

        DataIterator strs = currentProgram.getListing().getDefinedData(true);
        for (Data d : strs) {
            if (monitor.isCancelled()) break;
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (!isAnchor(s)) continue;
            Address strAddr = d.getAddress();
            if (strAddrByName.containsKey(s)) continue; // first occurrence
            strAddrByName.put(s, strAddr);
            List<Function> fns = new ArrayList<>();
            for (Reference r : rm.getReferencesTo(strAddr)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) { fns.add(f); allRefFns.add(f); }
            }
            byName.put(s, fns);
        }

        println("=== anchor string -> referencing functions ===");
        for (String name : ANCHORS) {
            Address a = strAddrByName.get(name);
            if (a == null) { println(String.format("  %-52s  (NOT FOUND as defined data)", name)); continue; }
            List<Function> fns = byName.get(name);
            List<String> tags = new ArrayList<>();
            for (Function f : fns) tags.add(f.getName() + "@" + f.getEntryPoint());
            println(String.format("  %-52s @%s  refd-by=%s", name, a,
                    tags.isEmpty() ? "(none -- runtime/data-only)" : String.join(", ", tags)));
        }

        // RTTI / symbol scan for CScriptBind_Entity + CallScriptFunction members.
        println("\n=== symbols matching ScriptBind_Entity / CallScriptFunction ===");
        SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
        int shown = 0;
        for (Symbol sym : syms) {
            if (monitor.isCancelled()) break;
            String n = sym.getName();
            if (n == null) continue;
            if (n.contains("ScriptBind_Entity") || n.contains("CallScriptFunction")
                    || n.contains("SendScriptEvent") || n.contains("CallStateFunction")) {
                println(String.format("  %-60s @%s  (%s)", n, sym.getAddress(), sym.getSymbolType()));
                if (++shown > 80) { println("  … (capped)"); break; }
            }
        }

        // Decompile each referencing function. Read for the dispatch shape:
        // BeginCall(funcName) + EndCall on a per-entity script table, or a single
        // central fan-out. Cap output per fn.
        println("\n=== " + allRefFns.size() + " referencing functions (decompiled, capped) ===");
        for (Function f : allRefFns) {
            if (monitor.isCancelled()) break;
            println("\n" + "-".repeat(72));
            println("FUNC " + f.getName() + " @ " + f.getEntryPoint()
                    + "  size=" + f.getBody().getNumAddresses());
            DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted()) {
                String[] lines = dr.getDecompiledFunction().getC().split("\n");
                int limit = Math.min(lines.length, 90);
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
