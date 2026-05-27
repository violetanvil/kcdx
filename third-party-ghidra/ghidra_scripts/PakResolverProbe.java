// Phase 8.5a: identify the CCryPak open-by-path (FOpen) vtable method + ABI.
//
// Known from capstone recon (_research/phase8.5-pak-resolver/):
//   CCryPak vtable @ 0x183A95FA8 (image base 0x180000000)
//   FOpen-impl (owns "ERROR FOpen '%s'" string) @ 0x182440D80
//
// This script:
//  (1) Decompiles the FOpen impl 0x182440D80 -> its signature/ABI.
//  (2) Finds callers of 0x182440D80; reports which are CCryPak vtable slots.
//  (3) Decompiles each vtable slot that calls it -> the public FOpen method
//      + its signature (this should be the open-by-path resolver).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

public class PakResolverProbe extends GhidraScript {
    static final long BASE = 0x180000000L;
    static final long VTABLE = 0x183A95FA8L;
    static final long FOPEN_IMPL = 0x182440D80L;

    AddressSpace sp;

    Address a(long va){ return sp.getAddress(va); }

    String decompile(DecompInterface di, Function f, int max) {
        if (f == null) return "(no function)";
        DecompileResults r = di.decompileFunction(f, 30, monitor);
        if (r == null || !r.decompileCompleted()) return "(decompile failed)";
        String c = r.getDecompiledFunction().getC();
        if (c.length() > max) c = c.substring(0, max) + "\n...[truncated]";
        return c;
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // Read vtable slots into a map (fn VA -> slot index)
        java.util.HashMap<Long,Integer> slotOf = new java.util.HashMap<>();
        println("=== CCryPak vtable @ 0x" + Long.toHexString(VTABLE) + " ===");
        for (int i = 0; i < 80; i++) {
            long fn = mem.getLong(a(VTABLE + i*8L));
            Function f = getFunctionAt(a(fn));
            String nm = (f!=null)? f.getName() : "(no func)";
            println(String.format("  slot[%2d] +0x%02X -> 0x%X  %s", i, i*8, fn, nm));
            slotOf.put(fn, i);
        }

        // (1) FOpen impl signature
        Function fopen = getFunctionAt(a(FOPEN_IMPL));
        if (fopen == null) {
            // try containing
            fopen = getFunctionContaining(a(FOPEN_IMPL));
        }
        println("\n=== FOpen-impl @ 0x" + Long.toHexString(FOPEN_IMPL) + " ===");
        if (fopen != null) {
            println("  name: " + fopen.getName());
            println("  signature: " + fopen.getSignature().getPrototypeString());
            println("  paramCount: " + fopen.getParameterCount() + "  callingConv: " + fopen.getCallingConventionName());
            println("  --- decompiled (head) ---");
            println(decompile(di, fopen, 2500));
        }

        // (2) callers of FOpen impl
        println("\n=== callers of FOpen-impl 0x" + Long.toHexString(FOPEN_IMPL) + " ===");
        Reference[] refs = getReferencesTo(a(FOPEN_IMPL));
        java.util.HashSet<Long> callerFns = new java.util.HashSet<>();
        for (Reference rf : refs) {
            Address from = rf.getFromAddress();
            Function cf = getFunctionContaining(from);
            long cva = (cf!=null)? cf.getEntryPoint().getOffset() : 0;
            String slot = slotOf.containsKey(cva) ? ("  <== CCryPak vtable slot["+slotOf.get(cva)+"] (+0x"+Integer.toHexString(slotOf.get(cva)*8)+")") : "";
            println(String.format("  ref from 0x%X  type=%s  in fn %s 0x%X%s",
                from.getOffset(), rf.getReferenceType(), (cf!=null?cf.getName():"?"), cva, slot));
            if (cf!=null) callerFns.add(cva);
        }

        // (3) decompile vtable slots that are callers (the public FOpen method)
        println("\n=== decompile vtable-slot callers (the public open-by-path method) ===");
        for (long cva : callerFns) {
            if (!slotOf.containsKey(cva)) continue;
            Function f = getFunctionAt(a(cva));
            println("\n--- slot[" + slotOf.get(cva) + "] (+0x" + Integer.toHexString(slotOf.get(cva)*8) + ") fn 0x" + Long.toHexString(cva) + " ---");
            println("  signature: " + f.getSignature().getPrototypeString());
            println("  paramCount: " + f.getParameterCount() + "  callingConv: " + f.getCallingConventionName());
            println(decompile(di, f, 2000));
        }
        println("\ndone.");
    }
}
