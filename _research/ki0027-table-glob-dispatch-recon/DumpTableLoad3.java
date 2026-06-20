// KI-0027 step 3: decompile FUN_180ef8588 (next loader layer) and any callee that
// references a '*' glob or a CCryPak enumeration. Print C + list its called functions.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class DumpTableLoad3 extends GhidraScript {
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
        // list direct calls out
        Function[] called = f.getCalledFunctions(monitor).toArray(new Function[0]);
        println("  --- calls out ---");
        for (Function c : called) println("    0x" + c.getEntryPoint() + "  " + c.getName());
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        dump(0x180ef8588L, "FUN_180ef8588 — loader layer 3");
        println("\n========== done ==========");
    }
}
