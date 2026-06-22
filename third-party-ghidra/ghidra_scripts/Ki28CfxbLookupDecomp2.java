// KI-0028 follow-up: read the callers/branches that gate use-vs-recompile.
//   FUN_180b033a0 (caller of lookupdata.bin parse) — what calls the cache open/validate
//   FUN_180da342c (caller of ShaderCacheMisses.txt reader)
//   FUN_180bb190c (globals.txt open-fail -> cache invalidate/rebuild)
//   FUN_180b04984 (the lookupdata.bin loader whose bool return gates "disable read-only cache")
//   FUN_180b04660 (staticmacrolist.bin loader, second gate arm)
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class Ki28CfxbLookupDecomp2 extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  size: " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        decompFull(0x180b04984L, "FUN_180b04984 (lookupdata.bin LOADER - bool return gates disable-cache)");
        decompFull(0x180b04660L, "FUN_180b04660 (staticmacrolist.bin loader, 2nd gate arm)");
        decompFull(0x180b033a0L, "FUN_180b033a0 (caller of cache open/validate FUN_180b04478)");
        decompFull(0x180bb190cL, "FUN_180bb190c (globals.txt open-fail -> invalidate/rebuild)");
        decompFull(0x180da342cL, "FUN_180da342c (caller of ShaderCacheMisses.txt reader)");
        println("\ndone.");
    }
}
