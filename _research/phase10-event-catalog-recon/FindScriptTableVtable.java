// FindScriptTableVtable.java -- pin the concrete CScriptTable vtable and read its
// slot +0xb0 (the per-table CallFunction dispatcher the entity-script fire sites
// invoke). Route: CScriptSystem vtable (RVA 0x3B8AF70) slot 13 = CreateTable;
// CreateTable allocates a CScriptTable and installs its vtable -> read that vtable
// from the CreateTable body, then dump slot +0xb0 and decompile its target.
// READ bodies; never infer.
//@category Research

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class FindScriptTableVtable extends GhidraScript {

    private final long IB = 0x180000000L;

    private void decompVA(DecompInterface di, long va, String why) {
        Address a = toAddr(va);
        Function f = getFunctionContaining(a);
        if (f == null) { println("\n[" + why + "] no fn at " + a); return; }
        println("\n" + "-".repeat(72));
        println("DECOMP " + why + ": " + f.getName() + " @ " + f.getEntryPoint()
                + " (RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - IB)
                + ")  size=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            String[] ls = dr.getDecompiledFunction().getC().split("\n");
            int lim = Math.min(ls.length, 120);
            for (int i = 0; i < lim; i++) println("  " + ls[i]);
            if (ls.length > lim) println("  … (" + (ls.length - lim) + " more)");
        } else println("  <decompile failed>");
    }

    private void dumpVtable(long vtblVA, int n, String label) throws Exception {
        println("\n=== vtable " + label + " @ 0x" + Long.toHexString(vtblVA) + " ===");
        for (int i = 0; i < n; i++) {
            long t = currentProgram.getMemory().getLong(toAddr(vtblVA + (long) i * 8));
            if (t == 0) { println(String.format("  slot[%2d] off=0x%-3x -> 0", i, i * 8)); continue; }
            Function f = getFunctionAt(toAddr(t));
            println(String.format("  slot[%2d] off=0x%-3x -> 0x%x (RVA 0x%x) %s",
                    i, i * 8, t, t - IB, f != null ? f.getName() : "(no fn)"));
        }
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // CScriptSystem vtable @ RVA 0x3B8AF70 (DB id 119). slot 13 = CreateTable.
        long ssVtbl = IB + 0x3B8AF70L;
        long createTable = currentProgram.getMemory().getLong(toAddr(ssVtbl + 13 * 8));
        println("CScriptSystem vtable slot[13] (CreateTable) -> 0x" + Long.toHexString(createTable)
                + " (RVA 0x" + Long.toHexString(createTable - IB) + ")");
        decompVA(di, createTable, "IScriptSystem::CreateTable (slot 13) — installs CScriptTable vtable");

        // slot 12 too (front-3 / DB note: both 12 and 13 call lua_createtable).
        long s12 = currentProgram.getMemory().getLong(toAddr(ssVtbl + 12 * 8));
        decompVA(di, s12, "CScriptSystem slot[12]");

        di.dispose();
        println("\n=== done ===");
    }
}
