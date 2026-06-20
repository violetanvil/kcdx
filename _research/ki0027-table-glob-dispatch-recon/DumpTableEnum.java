// KI-0027 step 4: the 0x181e39 cluster is the table-FILE enumerator (the __ xref lives
// in FUN_181e39b30). Decompile the enumerator chain and report CCryPak vtable dispatch.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class DumpTableEnum extends GhidraScript {
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
        dump(0x181e399c0L, "FUN_181e399c0 — table descriptor builder");
        dump(0x181e39b30L, "FUN_181e39b30 — the __ glob site");
        println("\n========== done ==========");
    }
}
