// KI-0028 .cfxb consumer + cache-lookup/validation recon.
// Decompile + raw-disasm the .cfxb direct consumers and the cache-lookup/validation path:
//   FUN_180774fac, FUN_18091f4f4 (.cfxb literal 0x183a504bc)
//   FUN_180b04478 (lookupdata.bin parse)
//   FUN_1819dfb54 (ShaderCacheMisses.txt recorder)
//   FUN_180bb058c (Shaders/Cache/globals.txt)
// Also dump callers (referencesTo) for the lookup parse + the miss recorder so the
// use-cache-vs-recompile branch can be located.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class Ki28CfxbLookupDecomp extends GhidraScript {
    AddressSpace sp;
    Memory mem;
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

    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mn = ins.toString();
            Address[] fl = ins.getFlows();
            StringBuilder ann = new StringBuilder();
            if (fl != null) {
                for (Address t : fl) {
                    Function tf = getFunctionAt(t);
                    if (tf == null) tf = getFunctionContaining(t);
                    ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                    if (tf != null) ann.append(" (").append(tf.getName()).append(")");
                }
            }
            println("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann);
            if (++n > 1200) { println("  ...[trunc disasm]"); break; }
        }
    }

    void callers(long va, String label) {
        println("\n===== CALLERS of " + label + " @ 0x" + Long.toHexString(va) + " =====");
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a(va));
        int n = 0;
        while (ri.hasNext()) {
            Reference r = ri.next();
            Address from = r.getFromAddress();
            Function ff = getFunctionContaining(from);
            println("  from 0x" + Long.toHexString(from.getOffset())
                + "  type=" + r.getReferenceType()
                + (ff != null ? "  in " + ff.getName() + " @0x" + Long.toHexString(ff.getEntryPoint().getOffset()) : "  (no fn)"));
            if (++n > 60) { println("  ...[trunc callers]"); break; }
        }
        if (n == 0) println("  (no references)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. .cfxb direct consumers
        decompFull(0x180774facL, "FUN_180774fac (.cfxb consumer A)");
        disasmFn(0x180774facL, "FUN_180774fac");
        decompFull(0x18091f4f4L, "FUN_18091f4f4 (.cfxb consumer B)");
        disasmFn(0x18091f4f4L, "FUN_18091f4f4");

        // 2. lookupdata.bin parse + its callers (use-cache-vs-recompile gate)
        decompFull(0x180b04478L, "FUN_180b04478 (lookupdata.bin parse)");
        disasmFn(0x180b04478L, "FUN_180b04478");
        callers(0x180b04478L, "FUN_180b04478 (lookupdata.bin parse)");

        // 3. ShaderCacheMisses.txt recorder + its callers (the swap-on .ext reprobe?)
        decompFull(0x1819dfb54L, "FUN_1819dfb54 (ShaderCacheMisses.txt recorder)");
        disasmFn(0x1819dfb54L, "FUN_1819dfb54");
        callers(0x1819dfb54L, "FUN_1819dfb54 (ShaderCacheMisses.txt recorder)");

        // 4. globals.txt handler
        decompFull(0x180bb058cL, "FUN_180bb058c (Shaders/Cache/globals.txt)");
        disasmFn(0x180bb058cL, "FUN_180bb058c");

        println("\ndone.");
    }
}
