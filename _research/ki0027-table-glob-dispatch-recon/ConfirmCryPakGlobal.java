// KI-0027 step 8: confirm whether DAT_18492b850 is the CCryPak singleton.
// Method: decompile a couple of its READ-consumers that are KNOWN CCryPak ops to see if
// they dispatch the SAME slots as the CCryPak vtable. Also decompile FUN_18041d238 and
// FUN_180460364 (low-RVA = CryPak core) which read it, and print bytes of CCryPak vtable
// around +0x1f8..+0x208 vs the slot101 region to map the two enumeration APIs.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class ConfirmCryPakGlobal extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void dump(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n--- 0x" + Long.toHexString(va) + " (" + label + ") ---");
        if (f == null) { println("  (no fn)"); return; }
        println("  name=" + f.getName());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // FUN_180460364 reads DAT_18492b850 and is at CryPak-core RVA — likely FOpen-ish, will show
        // whether the global is the CryPak singleton (calls AdjustFileName slot1 etc on it).
        dump(0x180460364L, "CryPak-core reader of the global");
        dump(0x18041d238L, "another low-RVA reader of the global");
        dump(0x180973058L, "the +0x1f8 fn (FindFirst by handle) the table glob calls");
        dump(0x18097383cL, "the +0x208 fn (FindClose) the table glob calls");

        // Is DAT_18492b850's vtable the CCryPak vtable? Check: does FUN_180460364 dispatch via
        // a global whose vtable matches 0x183A95FA8? We compare by reading what the glob fn's
        // +0x1f8 target (0x180973058) does internally vs CCryPak FindFirst factory 0x180973294.
        dump(0x180973294L, "CCryPak slot101 FindFirst factory (+0x328) for contrast");

        println("\n========== done ==========");
    }
}
