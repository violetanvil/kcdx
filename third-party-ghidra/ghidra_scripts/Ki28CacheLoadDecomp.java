// KI-0028 Layer-B FRONT: CShaderMan cache-load -> shader-list-SUBMIT path.
// Read-only decompile of the cache-load/precache/parse functions + their callers,
// dumping call edges so the "cache-blob-in-registry -> submit for pipeline build" gate
// is visible. Templated off PakSubResolverDecomp.java.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class Ki28CacheLoadDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount());
        println("  entry: 0x" + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) {
            println(r.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
        }
    }

    // list the callers of a function (who references its entry point)
    void callers(long va, String label) {
        println("\n----- CALLERS of " + label + " @ 0x" + Long.toHexString(va) + " -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator refs = rm.getReferencesTo(a(va));
        int n = 0;
        while (refs.hasNext()) {
            Reference r = refs.next();
            Address from = r.getFromAddress();
            Function f = getFunctionContaining(from);
            String fn = (f != null) ? f.getName() : "<no-fn>";
            Address fe = (f != null) ? f.getEntryPoint() : from;
            println("    from=0x" + Long.toHexString(from.getOffset()) + " fn=" + fn
                + " entry=0x" + Long.toHexString(fe.getOffset()) + " type=" + r.getReferenceType());
            if (++n > 30) { println("    ...(more)"); break; }
        }
        if (n == 0) println("    (no references)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // === 1. CShaderMan::_PrecacheShaderList (Layer B core) ===
        decompFull(0x1825091e0L, "FUN_1825091e0 = CShaderMan::_PrecacheShaderList");
        callers(0x1825091e0L, "_PrecacheShaderList");

        // === 2. other ShaderCache.cpp functions ===
        decompFull(0x1819d4a54L, "FUN_1819d4a54 (ShaderCache.cpp)");
        decompFull(0x18250dd8cL, "FUN_18250dd8c (ShaderCache.cpp)");

        // === 3. CShaderManBin::LoadBinShader (.cfxb blob parser) ===
        decompFull(0x180920048L, "FUN_180920048 = CShaderManBin::LoadBinShader");
        callers(0x180920048L, "LoadBinShader");

        // === downstream PSO/GPU-object create end (for edge check) ===
        decompFull(0x18252f3acL, "FUN_18252f3ac = CHWShader_D3D::mfUploadHW (blob->GPU object)");
        callers(0x18252f3acL, "mfUploadHW");

        println("\ndone.");
    }
}
