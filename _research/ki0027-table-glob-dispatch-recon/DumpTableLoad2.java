// KI-0027 step 2: follow into the actual table-load worker FUN_180ef83dc and the
// per-table discovery it calls. Dump full C for each so the glob->CCryPak dispatch is read.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class DumpTableLoad2 extends GhidraScript {
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
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        dump(0x180ef83dcL, "FUN_180ef83dc — table-load worker (returns false -> fatal)");

        println("\n========== done ==========");
    }
}
