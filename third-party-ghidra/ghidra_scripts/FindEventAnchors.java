// FindEventAnchors.java -- locate the script-driven gameplay-event anchors in
// WHGame.dll for Phase 10 front 3 (perk_unlocked / level_up /
// quest_stage_advanced / dialogue_line_spoken).
//
// Question (theory-INDEPENDENT, ground-truth first): for each candidate event
// name string, WHO references it, and does the referencing function look like
//   (a) a Lua-registration site (string passed to AddFunction / SCRIPT_REG /
//       RegisterFunction / a ScriptBind table) -> the game exposes it TO Lua,
//   (b) an internal C++ notify/dispatch (the C++ anchor), or
//   (c) only UI / log / data text (no event behind it).
// We do NOT assume (a); we READ each referencing body and report it.
//
// Read-only. Run:
//   analyzeHeadless <proj_dir> KCD2 -process WHGame.dll \
//       -postScript FindEventAnchors.java -noanalysis -readOnly
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
import ghidra.util.task.ConsoleTaskMonitor;

public class FindEventAnchors extends GhidraScript {

    // EXACT event-name string literals observed in the binary (front-3 events).
    // Exact match (not substring) so we don't drag in unrelated text.
    private static final String[] EVENT_NAMES = {
        "AddPerk", "LearnPerk", "PerkUsed", "ShowPerkUsed",
        "LevelUp", "ShowLevelUp", "LastSkillLevelUp", "LastStatLevelUp",
        "QuestStateChanged", "QuestObjectiveFinished", "ShowQuestEvent",
        "Dialog:PlayDialog", "FE_DialogueSpeaking"
    };

    private static boolean isEvent(String s) {
        for (String a : EVENT_NAMES) if (s.equals(a)) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Find each event-name string literal and its referencing functions.
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
            if (!isEvent(s)) continue;
            Address strAddr = d.getAddress();
            strAddrByName.put(s, strAddr);
            List<Function> fns = new ArrayList<>();
            for (Reference r : rm.getReferencesTo(strAddr)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) { fns.add(f); allRefFns.add(f); }
            }
            byName.put(s, fns);
        }

        println("=== event-name string -> referencing functions ===");
        for (String name : EVENT_NAMES) {
            Address a = strAddrByName.get(name);
            if (a == null) { println(String.format("  %-24s  (string literal NOT FOUND as defined data)", name)); continue; }
            List<Function> fns = byName.get(name);
            List<String> tags = new ArrayList<>();
            for (Function f : fns) tags.add(f.getName() + "@" + f.getEntryPoint());
            println(String.format("  %-24s @%s  refd-by=%s", name, a,
                    tags.isEmpty() ? "(none -- not xref'd, may be data-only/runtime)" : String.join(", ", tags)));
        }

        // 2. Decompile each referencing function. Read for the discriminator:
        //    a registration call (AddFunction / RegisterFunction / SCRIPT_REG / a
        //    ScriptBind 'this') vs an internal notify/log. Cap output per fn.
        println("\n=== " + allRefFns.size() + " referencing functions (decompiled, capped) ===");
        for (Function f : allRefFns) {
            if (monitor.isCancelled()) break;
            println("\n" + "-".repeat(72));
            println("FUNC " + f.getName() + " @ " + f.getEntryPoint()
                    + "  size=" + f.getBody().getNumAddresses());
            DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted()) {
                String[] lines = dr.getDecompiledFunction().getC().split("\n");
                int limit = Math.min(lines.length, 70);
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
