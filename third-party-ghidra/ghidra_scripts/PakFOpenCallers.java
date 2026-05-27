// Phase 8.5a corroboration: find REAL asset-read call sites that invoke the
// ICryPak FOpen vtable slot (+0x120) with a read mode, to confirm slot 36 is
// the open-by-path used for reads (not only the "wb" cache writer).
//
// Approach: xref the pCryPak global DAT_18492b850 (gEnv+0x50). For each function
// that reads it, decompile and look for a `(**(code**)(*pCryPak + 0x120))(...)`
// call -> print the function name + the decompiled call line context.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.*;

public class PakFOpenCallers extends GhidraScript {
    AddressSpace sp;
    Address a(long va){ return sp.getAddress(va); }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long pCryPak = 0x18492B850L;
        Reference[] refs = getReferencesTo(a(pCryPak));
        println("xrefs to pCryPak global (gEnv+0x50) @0x" + Long.toHexString(pCryPak) + ": " + refs.length);
        LinkedHashSet<Long> fns = new LinkedHashSet<>();
        for (Reference rf : refs) {
            Function cf = getFunctionContaining(rf.getFromAddress());
            if (cf != null) fns.add(cf.getEntryPoint().getOffset());
        }
        println("distinct functions reading pCryPak: " + fns.size());

        int hits = 0;
        for (long fva : fns) {
            Function f = getFunctionAt(a(fva));
            if (f == null) continue;
            DecompileResults r = di.decompileFunction(f, 25, monitor);
            if (r == null || !r.decompileCompleted()) continue;
            String c = r.getDecompiledFunction().getC();
            // look for the FOpen offset call signature
            if (c.contains("+ 0x120)") || c.contains("0x120))(")) {
                hits++;
                println("\n=== FN " + f.getName() + " @0x" + Long.toHexString(fva) + " calls +0x120 (FOpen) ===");
                // print only lines mentioning 0x120 with a little context
                String[] lines = c.split("\n");
                for (int i=0;i<lines.length;i++){
                    if (lines[i].contains("0x120")) {
                        int lo = Math.max(0,i-2), hi = Math.min(lines.length,i+2);
                        for (int j=lo;j<hi;j++) println("   " + lines[j]);
                        println("   ----");
                    }
                }
                if (hits >= 12) { println("\n(stopping at 12 hits)"); break; }
            }
        }
        println("\ntotal FOpen-calling functions shown: " + hits);
        println("done.");
    }
}
