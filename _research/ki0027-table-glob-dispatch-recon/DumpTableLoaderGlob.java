// KI-0027: which CCryPak slot does the Libs\Tables\ __*.xml override-glob dispatch through?
// Decompile the functions that reference the table-load string anchors and dump their C,
// so the actual glob construction -> CCryPak vtable dispatch is READ, not inferred.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class DumpTableLoaderGlob extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void dumpContaining(long va, String label) {
        Function f = getFunctionContaining(a(va));
        println("\n========== fn containing 0x" + Long.toHexString(va)
            + "  (" + label + ") ==========");
        if (f == null) { println("  (no fn)"); return; }
        println("  entry = 0x" + f.getEntryPoint() + "   name = " + f.getName());
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // The FOpen-by-name table site (builds Libs\Tables\ + __ + xml, FOpen slot36)
        dumpContaining(0x180D1D58EL, "0x180D1D site: Libs\\Tables\\+__ , FOpen(slot36)");
        // The fatal "Database system error - tables can't be loaded" raise site
        dumpContaining(0x180D1B928L, "0x180D1B site: Database-system-error raise");
        // The AdjustFileName-heavy XML-table manager
        dumpContaining(0x180EF7973L, "0x180EF79 site: Libs\\Tables\\ + AdjustFileName loop");
        dumpContaining(0x180EF81E6L, "0x180EF81 site: error string near EF function");
        // The lone __ xref via +0x10 vtable
        dumpContaining(0x181E39B4CL, "0x181E39 site: __ via +0x10 vtable");

        println("\n========== done ==========");
    }
}
