// FRONT 3 leaves: confirm the OS-arm CRT read + the pak-arm read + the handle-array accessor.
// From FRead (slot 40, FUN_18051cd00) / FSeek (slot 38) / FEof (slot 39):
//   - puVar = (FILE*)param_2 - 1 ; if (puVar < FUN_180427e40(this+8/0x40)) -> pak arm else OS arm.
//   - pak arm: FUN_18051ce40(this[8] + puVar*0x18, dst) ; OS arm: FUN_1804d7ab4(...) (= CRT fread?)
// This script:
//  (1) FUN_1804d7ab4 — the OS-arm read leaf (CRT fread tail?) FULL.
//  (2) FUN_18051ce40 — the pak-arm read leaf (reads from the 0x18-stride pak-handle struct) FULL.
//  (3) FUN_180427e40 — the handle-array count/begin accessor (the dispatch bound) FULL.
//  (4) FUN_18051ce40's children one level (where the pak bytes are actually produced).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class PakReadLeaves extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount() + "  size: " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        decompFull(0x1804d7ab4L, "FUN_1804d7ab4 — OS-arm read leaf (CRT fread?)");
        decompFull(0x180427e40L, "FUN_180427e40 — handle-array count/begin accessor (dispatch bound)");
        decompFull(0x18051ce40L, "FUN_18051ce40 — pak-arm read leaf (FRead pak branch)");
        decompFull(0x1804618b4L, "FUN_1804618b4 — pak-arm seek leaf (FSeek pak branch)");
        decompFull(0x18051e2bcL, "FUN_18051e2bc — pak-arm leaf (FEof/FTell pak branch)");

        println("\ndone.");
    }
}
