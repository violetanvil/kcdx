// FRONT 2 follow-up -- close the last two gaps:
//  (1) the directory-index BUILD: the archive-object factory (CCryPak vtable slot 72
//      = FUN_1804d5580, invoked via *(this+0x240) inside the per-part mount), and the
//      ZipDir Central-Directory parse leaf FUN_1804d6e70 / FUN_1804d6c18 (the CDREnd +
//      central-dir bounds reader -- the actual index build the resolver later searches).
//  (2) the OpenPacks (plural/glob) entry the mod-absorb code calls -- the register
//      worker FUN_1804d4824 has callers FUN_1804d9c4c and FUN_18193cb14; decompile both
//      + their callers to find the multi-pak/glob entry and its vtable slot.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class PakOpenMount2 extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    static final long BASE = 0x180000000L;
    Address a(long va){ return sp.getAddress(va); }
    long rva(long va){ return va - BASE; }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va)
            + " (RVA 0x" + Long.toHexString(rva(va)) + ") =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name " + f.getName() + " conv=" + f.getCallingConventionName()
            + " params=" + f.getParameterCount()
            + " sig: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    // find which CCryPak vtable slot holds a fn (AP3: read the table, not a label)
    void findSlot(long vtableVa, long fnVa, int slots, String label) throws MemoryAccessException {
        println("\n----- SLOT of " + label + " fn 0x" + Long.toHexString(fnVa) + " -----");
        boolean f = false;
        for (int i = 0; i < slots; i++) {
            long s = mem.getLong(a(vtableVa + (long) i * 8));
            if (s == fnVa) { println("  slot " + i + " (vtable+0x" + Long.toHexString(i * 8L) + ")"); f = true; }
        }
        if (!f) println("  NOT a vtable method in first " + slots + " slots");
    }

    void callers(long va, String label) {
        println("\n----- CALLERS of " + label + " 0x" + Long.toHexString(va) + " -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator ri = rm.getReferencesTo(a(va));
        int n = 0;
        while (ri.hasNext()) {
            Reference rf = ri.next();
            Address from = rf.getFromAddress();
            Function f = getFunctionContaining(from);
            println("  xref from 0x" + Long.toHexString(from.getOffset())
                + " type=" + rf.getReferenceType().getName()
                + " in fn=" + (f != null ? f.getName() + " (RVA 0x"
                    + Long.toHexString(rva(f.getEntryPoint().getOffset())) + ")" : "(none)"));
            n++;
        }
        println("  (total " + n + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        long VTABLE = 0x183A95FA8L;

        // (1) the archive factory (slot 72) + CDR build leaves
        long ARCH_FACTORY = 0x1804d5580L;  // slot 72 (this+0x240) -- opens file + builds index
        findSlot(VTABLE, ARCH_FACTORY, 96, "archive factory (this+0x240)");
        decompFull(ARCH_FACTORY, "FUN_1804d5580 archive-object factory (slot 72) -- builds ICryArchive");
        decompFull(0x1804d6e70L, "FUN_1804d6e70 ZipDir CDREnd/central-dir reader (index BUILD)");
        decompFull(0x1804d6c18L, "FUN_1804d6c18 ZipDir central-dir bounds reader");
        decompFull(0x1808b8ba8L, "FUN_1808b8ba8 ZipDir corrupted-CDR path");

        // (2) the OpenPacks (plural/glob) entry candidates
        decompFull(0x1804d9c4cL, "FUN_1804d9c4c register-worker caller (OpenPacks candidate)");
        decompFull(0x18193cb14L, "FUN_18193cb14 register-worker caller (OpenPacks candidate)");
        findSlot(VTABLE, 0x1804d9c4cL, 96, "FUN_1804d9c4c");
        findSlot(VTABLE, 0x18193cb14L, 96, "FUN_18193cb14");
        callers(0x1804d9c4cL, "FUN_1804d9c4c");
        callers(0x18193cb14L, "FUN_18193cb14");

        // the list-insert helper (rank-ordered insert into [this+0x120..0x128])
        decompFull(0x1804d70a4L, "FUN_1804d70a4 mounted-pak list rank-insert (into this+0x24/+0x120)");

        println("\nDONE.");
    }
}
