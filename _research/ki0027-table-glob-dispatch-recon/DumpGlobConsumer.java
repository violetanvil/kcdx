// KI-0027 step 6: FUN_180974484 sits in the CCryPak (0x180974xxx) cluster and references __.
// Decompile it + FUN_180696e08 (the __ string transform). Look for '*' append + the actual
// directory-scan dispatch (it may call FindFirst 0x180973294 / FindNext directly, not via vtable).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class DumpGlobConsumer extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void dump(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n========== 0x" + Long.toHexString(va) + "  (" + label + ") ==========");
        if (f == null) { println("  (no fn)"); return; }
        println("  entry = 0x" + f.getEntryPoint() + "   name = " + f.getName());
        DecompileResults r = di.decompileFunction(f, 180, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
        Function[] called = f.getCalledFunctions(monitor).toArray(new Function[0]);
        println("  --- calls out ---");
        for (Function c : called) println("    0x" + c.getEntryPoint() + "  " + c.getName());
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        dump(0x180974484L, "FUN_180974484 — CCryPak-cluster fn referencing __");
        dump(0x180696e08L, "FUN_180696e08 — __ string transform");
        dump(0x181e39830L, "FUN_181e39830 — descriptor builder body");
        println("\n========== done ==========");
    }
}
