// F4 (audio) recon — the FMOD read/size callbacks dispatch through CCryPak vtable
// +0x130 (slot 38) for READ and +0x170 (slot 46) for SIZE — NOT +0x140 (slot 40 =
// FGetCachedFileData, the verified Around-FOpen FRead seam). Read slot 38 + slot 46 bodies
// to state EXACTLY which read primitive audio reaches (AP19 — no inference from the slot label).
//   slot 38 = FUN_180461304 (front-1: "FOpen-by-pak-index/FReopen") @ 0x461304  <- READ callback target
//   slot 46 = FUN_180460c08 (front-1: "FEof/Fileno") @ 0x460c08                 <- SIZE callback target
// Also confirm DAT_18492b850 identity: dump the global + the CCryPak ctor that sets gEnv->pCryPak
// is already id 132; here just re-read slot 40 (FGetCachedFileData 0x51CD00) for contrast.
// Image base 0x180000000.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpFmodReadSlots extends GhidraScript {

    void dumpFunc(long fa, String label, DecompInterface di, AddressSpace sp) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================================");
        println(label + " @ " + a);
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION at this address"); return; }
        println("  entry: " + f.getEntryPoint() + "   proto: " + f.getPrototypeString(true, false));
        println("  size (addrs): " + f.getBody().getNumAddresses());
        println("  --- decompiled C ---");
        DecompileResults dr = di.decompileFunction(f, 180, new ConsoleTaskMonitor());
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

        dumpFunc(0x180461304L, "CCryPak slot 38 (+0x130) FUN_180461304 — the FMOD READ callback target", di, sp);
        dumpFunc(0x180460c08L, "CCryPak slot 46 (+0x170) FUN_180460c08 — the FMOD SIZE callback target", di, sp);
        dumpFunc(0x18051cd00L, "CCryPak slot 40 (+0x140) FGetCachedFileData 0x51CD00 — the verified FRead seam (contrast)", di, sp);

        println("\ndone.");
    }
}
