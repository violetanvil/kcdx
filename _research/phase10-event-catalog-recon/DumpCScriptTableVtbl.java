// DumpCScriptTableVtbl.java -- resolve the CScriptTable::vftable symbol, dump its
// slots (esp. +0xb0 = the CallFunction dispatcher the entity-script fire sites
// invoke), and decompile the +0xb0 target. READ; never infer.
//@category Research

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpCScriptTableVtbl extends GhidraScript {
    private final long IB = 0x180000000L;

    private void decompVA(DecompInterface di, long va, String why) {
        Function f = getFunctionContaining(toAddr(va));
        if (f == null) { println("\n[" + why + "] no fn at 0x" + Long.toHexString(va)); return; }
        println("\n" + "-".repeat(72));
        println("DECOMP " + why + ": " + f.getName() + " @ " + f.getEntryPoint()
                + " (RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - IB)
                + ")  size=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            String[] ls = dr.getDecompiledFunction().getC().split("\n");
            int lim = Math.min(ls.length, 130);
            for (int i = 0; i < lim; i++) println("  " + ls[i]);
            if (ls.length > lim) println("  … (" + (ls.length - lim) + " more)");
        } else println("  <decompile failed>");
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Address vtbl = null;
        SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
        for (Symbol s : syms) {
            if (s.getName() != null && s.getName().equals("vftable")
                    && s.getParentNamespace() != null
                    && s.getParentNamespace().getName().equals("CScriptTable")) {
                vtbl = s.getAddress(); break;
            }
        }
        if (vtbl == null) {
            // fallback: any symbol containing CScriptTable::vftable
            syms = currentProgram.getSymbolTable().getAllSymbols(true);
            for (Symbol s : syms) {
                String n = s.getName(true);
                if (n != null && n.contains("CScriptTable") && n.contains("vftable")) {
                    vtbl = s.getAddress(); println("matched: " + n + " @ " + vtbl); break;
                }
            }
        }
        if (vtbl == null) { println("CScriptTable::vftable NOT resolved"); di.dispose(); return; }

        long v = vtbl.getOffset();
        println("CScriptTable::vftable @ 0x" + Long.toHexString(v) + " (RVA 0x" + Long.toHexString(v - IB) + ")");
        println("\n=== slots 0..30 ===");
        for (int i = 0; i <= 30; i++) {
            long t = currentProgram.getMemory().getLong(toAddr(v + (long) i * 8));
            Function f = (t > IB && t < IB + 0x6000000L) ? getFunctionAt(toAddr(t)) : null;
            String mark = (i * 8 == 0xb0) ? "  <<<< +0xb0 (CallFunction dispatch)" : "";
            println(String.format("  slot[%2d] off=0x%-3x -> 0x%x (RVA 0x%x) %s%s",
                    i, i * 8, t, t - IB, f != null ? f.getName() : "(no fn)", mark));
        }

        long slotB0 = currentProgram.getMemory().getLong(toAddr(v + 0xb0));
        decompVA(di, slotB0, "slot +0xb0 = the per-script-table CallFunction dispatcher");

        di.dispose();
        println("\n=== done ===");
    }
}
