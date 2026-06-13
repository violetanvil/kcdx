// WalkPerkGrant.java -- Phase 10 perk_unlocked fire-site walk (front 3 left the
// fire-site one caller-hop unread). Pure static read.
//
// Theory-INDEPENDENT plan. Three independent bridges to the same target ("the
// function that grants/unlocks a perk"); we READ each body and report what it
// does, not what we expect:
//   BRIDGE A -- the Lua command impl. front 3 read FUN_181675cdc registering
//     FUN_180a52c20(param_1,"AddPerk","perk_id",param_1,&local_res8) with
//     local_res8 := FUN_182cee04c just before. So FUN_182cee04c is the C++ impl
//     of the Lua AddPerk command. Decompile it + its callees one level.
//   BRIDGE B -- the RTTR storm operator. front 3 named wh::rpgmodule::storm::addPerk
//     + C_LearnPerkEffect / C_AddPerkEffect. Resolve any symbol matching
//     addPerk / LearnPerkEffect / AddPerkEffect, decompile it + its callers.
//   BRIDGE C -- the interned event-key constants. The getters return
//     &DAT_1855e3888 (AddPerk), &DAT_1855e3890 (LearnPerk), &DAT_1855e38a0
//     (PerkUsed). Find who READS those DAT addresses (the event-key consumers =
//     the notify/fire site).
//
// Read-only. Run:
//   analyzeHeadless <proj> KCD2 -process WHGame.dll -postScript WalkPerkGrant.java -noanalysis -readOnly
//
//@category Research

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.ClangNode;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class WalkPerkGrant extends GhidraScript {

    private DecompInterface di;

    private String decompile(Function f, int capLines) {
        if (f == null) return "  <null fn>";
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr == null || !dr.decompileCompleted()) return "  <decompile failed>";
        String[] lines = dr.getDecompiledFunction().getC().split("\n");
        int limit = Math.min(lines.length, capLines);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < limit; i++) sb.append("  ").append(lines[i]).append("\n");
        if (lines.length > limit) sb.append("  ... (").append(lines.length - limit).append(" more lines)\n");
        return sb.toString();
    }

    private void dump(Function f, int capLines) {
        if (f == null) { println("  <null fn>"); return; }
        println("\n" + "=".repeat(72));
        println("FUNC " + f.getName() + " @ " + f.getEntryPoint()
                + "  size=" + f.getBody().getNumAddresses());
        println(decompile(f, capLines));
    }

    private Function fnAt(String hexVa) {
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseLong(hexVa, 16));
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        return f;
    }

    // Callees one level down: functions called from inside f (via flow refs).
    private List<Function> callees(Function f) {
        List<Function> out = new ArrayList<>();
        Set<Long> seen = new HashSet<>();
        if (f == null) return out;
        for (Address a : f.getBody().getAddresses(true)) {
            Reference[] refs = currentProgram.getReferenceManager().getReferencesFrom(a);
            for (Reference r : refs) {
                if (!r.getReferenceType().isCall()) continue;
                Function callee = getFunctionAt(r.getToAddress());
                if (callee != null && seen.add(callee.getEntryPoint().getOffset())) out.add(callee);
            }
        }
        return out;
    }

    private List<Function> callersOf(Function f) {
        List<Function> out = new ArrayList<>();
        Set<Long> seen = new HashSet<>();
        if (f == null) return out;
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
            if (!r.getReferenceType().isCall() && !r.getReferenceType().isData()) continue;
            Function c = getFunctionContaining(r.getFromAddress());
            if (c != null && seen.add(c.getEntryPoint().getOffset())) out.add(c);
        }
        return out;
    }

    @Override
    public void run() throws Exception {
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // ---- BRIDGE A: the AddPerk Lua-command C++ impl ----
        println("################ BRIDGE A: AddPerk command impl FUN_182cee04c ################");
        Function addPerkCmd = fnAt("182cee04c");
        dump(addPerkCmd, 120);
        println("\n---- BRIDGE A callees (one level) ----");
        for (Function c : callees(addPerkCmd)) {
            println("  callee: " + c.getName() + " @ " + c.getEntryPoint() + "  size=" + c.getBody().getNumAddresses());
        }
        // decompile the most promising callees (skip tiny CRT/string helpers)
        for (Function c : callees(addPerkCmd)) {
            if (c.getBody().getNumAddresses() < 24) continue;   // skip trivial helpers
            dump(c, 90);
        }

        // ---- BRIDGE B: RTTR storm::addPerk / *PerkEffect symbols ----
        println("\n\n################ BRIDGE B: storm::addPerk / *PerkEffect symbols ################");
        SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
        Set<Function> bridgeB = new LinkedHashSet<>();
        int scanned = 0;
        while (syms.hasNext()) {
            Symbol s = syms.next();
            scanned++;
            String n = s.getName();
            if (n == null) continue;
            String ln = n.toLowerCase();
            boolean hit = ln.contains("addperk") || ln.contains("learnperk")
                    || (ln.contains("perk") && (ln.contains("effect") || ln.contains("apply") || ln.contains("grant") || ln.contains("unlock")));
            if (!hit) continue;
            FunctionManager fm = currentProgram.getFunctionManager();
            Function f = fm.getFunctionAt(s.getAddress());
            if (f == null) f = fm.getFunctionContaining(s.getAddress());
            if (f != null) bridgeB.add(f);
            println("  SYM " + s.getName() + " @ " + s.getAddress() + (f != null ? ("  -> fn " + f.getName() + "@" + f.getEntryPoint()) : "  (no fn)"));
        }
        println("  (scanned " + scanned + " symbols; " + bridgeB.size() + " distinct fns)");
        for (Function f : bridgeB) {
            dump(f, 90);
            println("  ---- callers of " + f.getName() + " ----");
            for (Function c : callersOf(f)) println("    caller: " + c.getName() + " @ " + c.getEntryPoint());
        }

        // ---- BRIDGE C: consumers of the interned event-key DATs ----
        println("\n\n################ BRIDGE C: consumers of interned perk-key DATs ################");
        String[] dats = { "1855e3888", "1855e3890", "1855e38a0" };  // AddPerk / LearnPerk / PerkUsed
        String[] datNames = { "AddPerk-key", "LearnPerk-key", "PerkUsed-key" };
        Set<Function> bridgeC = new LinkedHashSet<>();
        for (int i = 0; i < dats.length; i++) {
            Address da = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.parseLong(dats[i], 16));
            println("\n  DAT " + datNames[i] + " @ " + da + " referenced by:");
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(da)) {
                Function c = getFunctionContaining(r.getFromAddress());
                String fn = (c != null) ? (c.getName() + "@" + c.getEntryPoint()) : "(no fn)";
                println("    from " + r.getFromAddress() + " type=" + r.getReferenceType() + " in " + fn);
                if (c != null) bridgeC.add(c);
            }
        }
        // Decompile every distinct consumer that is NOT one of the getter stubs already read.
        Set<String> known = new HashSet<>();
        known.add("FUN_1808fd090"); known.add("FUN_182cac05c"); known.add("FUN_18158524c");
        for (Function f : bridgeC) {
            if (known.contains(f.getName())) { println("\n  (skip known getter " + f.getName() + ")"); continue; }
            dump(f, 90);
        }

        di.dispose();
        println("\n=== done ===");
    }
}
