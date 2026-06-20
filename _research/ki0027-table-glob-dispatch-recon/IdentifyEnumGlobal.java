// KI-0027 step 7: identify DAT_18492b850 (the enumerator the table glob dispatches through)
// and its vtable +0x1f8/+0x200/+0x208. Is it CCryPak (gEnv+0x50 / vtable 0x183A95FA8), or a
// separate IArchive/IResourceList enumerator? Find writes to the global + its RTTI.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

public class IdentifyEnumGlobal extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long GLOBAL = 0x18492b850L;
        println("=== references to DAT_18492b850 (write = assignment of the enumerator) ===");
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a(GLOBAL))) {
            Function f = getFunctionContaining(r.getFromAddress());
            String fn = (f != null) ? (f.getName() + " @ 0x" + f.getEntryPoint()) : "(none)";
            println("  " + r.getReferenceType() + " from 0x" + r.getFromAddress() + "  in " + fn);
        }

        // Decompile the smallest function that WRITES the global (the init/getter)
        println("\n=== functions writing DAT_18492b850 (decompiled) ===");
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a(GLOBAL))) {
            if (r.getReferenceType().isWrite()) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                println("\n--- WRITE site fn " + f.getName() + " @ 0x" + f.getEntryPoint() + " ---");
                DecompileResults dr = di.decompileFunction(f, 90, monitor);
                if (dr != null && dr.decompileCompleted()) println(dr.getDecompiledFunction().getC());
            }
        }

        // CCryPak vtable for comparison
        println("\n=== CCryPak vtable @ 0x183A95FA8 slot at +0x1f8 / +0x200 / +0x208 (for compare) ===");
        for (long off : new long[]{0x1f8, 0x200, 0x208}) {
            long slotVA = 0x183A95FA8L + off;
            ghidra.program.model.mem.Memory mem = currentProgram.getMemory();
            try {
                long fn = mem.getLong(a(slotVA));
                println("  CCryPak +0x" + Long.toHexString(off) + " -> 0x" + Long.toHexString(fn));
            } catch (Exception e) { println("  +0x"+Long.toHexString(off)+" read fail"); }
        }
        println("\n(CCryPak slot14 ForEachFile = 0x18241d2e8 @ +0x70; slot101 FindFirst = 0x180973294 @ +0x328)");
        println("\n========== done ==========");
    }
}
