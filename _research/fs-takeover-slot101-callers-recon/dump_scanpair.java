// slot-101 recon, pass 5 — confirm the iterator scan walks pak+disk, and verify the
// scan-entry edge to FUN_180973220.
//
// Established: FUN_180462af8 (slot-2 helper's pak arm) walks the LOADED-PAK vector
// [+0x120..+0x128] and each pak's zip directory (FUN_180463d04/FUN_1804635e8) — pak-dir
// enumeration, NOT disk. The disk arm is FUN_1809b249c. This pass:
//  (1) FUN_1809b249c — the disk arm (expect AdjustFileName + _findfirst64).
//  (2) FUN_180973220 callers — confirm it is reached as the iterator FindFirst/FindNext
//      (read in the caller body, AP19), and dump a couple of those consumer bodies to
//      confirm the `new CCryPakFindData (slot101) -> scan` shape.
//  (3) one real +0x328 consumer decompiled in full — confirm `this` is a CCryPak* (calls
//      other CCryPak slots) and what it does with the iterator (asset enumeration vs not).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class dump_scanpair extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        println("  name: " + f.getName() + "  size: " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }
    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface(); di.openProgram(currentProgram);

        // (1) disk arm.
        decompFull(0x1809b249cL, "FUN_1809b249c (iterator DISK arm)");

        // (2) callers of the scan helper FUN_180973220.
        println("\n========== callers of FUN_180973220 (iterator scan helper) ==========");
        Function tgt = getFunctionAt(a(0x180973220L));
        if (tgt != null) {
            ReferenceIterator rit = currentProgram.getReferenceManager().getReferencesTo(tgt.getEntryPoint());
            java.util.Set<Long> callers = new java.util.LinkedHashSet<>();
            while (rit.hasNext()) {
                Reference rf = rit.next();
                long from = rf.getFromAddress().getOffset();
                String t = rf.getReferenceType().toString();
                Function cf = getFunctionContaining(a(from));
                println("  ref from 0x" + Long.toHexString(from) + " type=" + t
                    + (cf != null ? " in " + cf.getName() : " (no fn)"));
                if (cf != null && t.toLowerCase().contains("call")) callers.add(cf.getEntryPoint().getOffset());
            }
            int n = 0;
            for (Long c : callers) { if (n++ >= 4) break; decompFull(c, "973220 caller #" + n); }
        }

        // (3) a real +0x328 consumer (first from the prior list).
        decompFull(0x180423b18L, "+0x328 consumer FUN_180423b18 (does it use the iterator for assets?)");

        di.dispose();
        println("\n===== dump_scanpair COMPLETE =====");
    }
}
