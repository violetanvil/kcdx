// Decompile CCryPak::FOpen (slot 36, id 131, RVA 0x4614A0) to read the OS/loose-file
// RETURN PATH: what handle structure it constructs + returns when it opens a loose file
// via the OS (the non-pak branch). The static precondition for an Around-FOpen hook that
// returns kcdx's OWN opened loose handle — the hook must mint a handle of FOpen's exact
// OS-path shape so FRead's else-arm (index >= pak-count) serves it.
// FRead (0x51CD00) dispatch already body-read: index = &handle[-1]._tmpfname+7 vs pak-count;
// this reads the PRODUCER side (FOpen) the consumer side implies but does not prove.
// Companion to _research/phase8.5-pak-resolver/ (asset-system seam).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpFOpenHandleConstruct extends GhidraScript {

    void dumpFunc(long fa, String label, DecompInterface di, AddressSpace sp) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================================");
        println(label + " @ " + a);
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION at this address"); return; }
        println("  Ghidra inferred prototype: " + f.getPrototypeString(true, false));
        println("  Function size (addrs): " + f.getBody().getNumAddresses());
        println("  --- decompiled C ---");
        DecompileResults dr = di.decompileFunction(f, 120, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            println(dr.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ")");
        }
    }

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // image base 0x180000000
        // FOpen itself — read the OS/loose-file branch's handle construction + return.
        dumpFunc(0x1804614A0L, "CCryPak::FOpen (id 131, slot 36, RVA 0x4614A0)", di, sp);

        // The leaves FOpen's disasm shows it calling on the open path — decompile them so the
        // handle-construction (alloc + field writes) is readable, not just a call target:
        //   0x4613d0 — called early (0x180461509), likely a normalize/setup helper
        //   0x462ddc — called mid-body (0x180461632)
        //   0x4613fc — called late  (0x180461708), near the return paths
        // FRead's OS arm read handle[-1]._tmpfname+7 as the index; whichever leaf writes that
        // region is the handle constructor.
        // CORRECTED again (post-HALT): the gate's three unread links.
        // LINK 2 (the decider): FRead's dispatch is `CMP R8,RAX; JNC` where R8 = handle-1
        // (0x18051cd3a LEA R8,[RBP-1]) and RAX = FUN_180427e40([RBX+0x40], handle-1).
        // What FUN_180427e40 RETURNS decides everything: if it returns the element COUNT,
        // then handle-1 is compared as an index (a _wfopen FILE* minus 1 is astronomically
        // large >= count -> else/OS arm). Read its body.
        dumpFunc(0x180427e40L, "FRead bound FUN_180427e40 (RAX in CMP R8,RAX — count? or transform?)", di, sp);
        // FRead also calls FUN_1804613d0 @0x18051cd23 with the handle BEFORE the compare —
        // read it: does it transform the handle or just set up a scratch struct?
        dumpFunc(0x1804613d0L, "FRead/FOpen prologue helper FUN_1804613D0 (handle setup?)", di, sp);
        // LINK 1: the loose-handle producer (verified _wfopen FILE*) + the alt-path producer.
        dumpFunc(0x1809b2b28L, "FOpen loose-handle producer 0x9B2B28 (returns _wfopen FILE*)", di, sp);
        dumpFunc(0x182423e08L, "FOpen alt-handle producer 0x2423E08 (param_4&0x10 branch)", di, sp);

        println("\ndone.");
    }
}
