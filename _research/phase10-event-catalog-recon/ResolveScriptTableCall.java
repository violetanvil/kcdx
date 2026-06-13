// ResolveScriptTableCall.java -- resolve the IScriptTable call dispatch slot
// (+0xb0) that the entity-script fire sites (OnUse/OnPickup) invoke, and read
// the dispatcher body. Also resolve the OnUse marshaller callback FUN_180ac8fb0.
//
// The OnUse fire site FUN_180efb548 does: r8 = [entity+0x48] (script table);
// rax = [r8]; call [rax + 0xb0]. So slot +0xb0 on the script-table vtable is the
// per-table CallFunction dispatcher. Find the concrete CScriptTable vtable, read
// slot +0xb0's target, decompile it. READ bodies; never infer.
//
// Read-only.
//@category Research

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class ResolveScriptTableCall extends GhidraScript {

    private void dumpSlots(Address vtbl, String label, int nSlots) throws MemoryAccessException {
        long base = currentProgram.getImageBase().getOffset();
        println("\n=== vtable " + label + " @ " + vtbl + " (slot -> target RVA) ===");
        for (int i = 0; i < nSlots; i++) {
            Address slotAddr = vtbl.add((long) i * 8);
            long target = currentProgram.getMemory().getLong(slotAddr);
            if (target == 0) continue;
            Function f = getFunctionAt(toAddr(target));
            String fn = (f != null) ? f.getName() : "(no fn)";
            println(String.format("  slot[%2d] off=0x%-3x -> 0x%x (RVA 0x%x)  %s",
                    i, i * 8, target, target - base, fn));
        }
    }

    private void decomp(DecompInterface di, long va, String why) {
        Address a = toAddr(va);
        Function f = getFunctionContaining(a);
        if (f == null) { println("\n[" + why + "] no function at " + a); return; }
        println("\n" + "-".repeat(72));
        println("DECOMP " + why + ": " + f.getName() + " @ " + f.getEntryPoint()
                + " (RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset()
                - currentProgram.getImageBase().getOffset()) + ")  size="
                + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            String[] lines = dr.getDecompiledFunction().getC().split("\n");
            int limit = Math.min(lines.length, 110);
            for (int i = 0; i < limit; i++) println("  " + lines[i]);
            if (lines.length > limit) println("  … (" + (lines.length - limit) + " more lines)");
        } else println("  <decompile failed>");
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Find CScriptTable / IScriptTable RTTI vtable label(s).
        println("=== symbols matching ScriptTable vftable / RTTI ===");
        SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
        int shown = 0;
        for (Symbol sym : syms) {
            if (monitor.isCancelled()) break;
            String n = sym.getName();
            if (n == null) continue;
            String nl = n.toLowerCase();
            if ((nl.contains("scripttable") || nl.contains("script_table"))
                    && (nl.contains("vftable") || nl.contains("vtable") || n.contains("::`"))) {
                println(String.format("  %-58s @%s  (%s)", n, sym.getAddress(), sym.getSymbolType()));
                if (++shown > 40) { println("  …(capped)"); break; }
            }
        }

        // 2. The OnUse marshaller callback the descriptor carries (FUN_180ac8fb0).
        decomp(di, 0x180ac8fb0L, "OnUse-marshaller-callback (descriptor code ptr)");

        // 3. The OnUse fire site itself for the record.
        decomp(di, 0x180efb548L, "OnUse-fire-site FUN_180efb548");

        di.dispose();
        println("\n=== done ===");
    }
}
