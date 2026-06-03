// Phase 8.5 FRONT-1: full-decompile the CCryPak vtable slots whose ROLE the one-line
// fingerprint left ambiguous, so each role is named on a decompiled body (AP1/AP2), not
// inferred from a canonical header (AP3). Targets chosen from _front1_surface_raw.txt.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;

public class PakVtableRoles extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void dump(int slot, long va, String guess) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n========== slot " + slot + "  RVA 0x" + Long.toHexString(va - 0x180000000L)
            + "  (" + guess + ") ==========");
        if (f == null) { println("  (no fn)"); return; }
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        dump(13, 0x182419280L, "FindFirst?");
        dump(14, 0x18241d2e8L, "FindNext?");
        dump(33, 0x18241a4e4L, "OpenArchive/mount?");
        dump(35, 0x182418de4L, "FOpenRaw/FAttachFileToPak?");
        dump(38, 0x180461304L, "FOpen-variant / FReopen?");
        dump(45, 0x182418b48L, "IsFileExist / disk+pak existence?");
        dump(64, 0x18041d640L, "FGetSize?");
        dump(67, 0x180463ec4L, "AdjustFileName?");
        dump(68, 0x18241ac8cL, "GetFileAttributes / IsFolder?");
        dump(70, 0x18241abccL, "IsFileExist variant?");
        dump(81, 0x18241769cL, "GetAlias?");
        dump(82, 0x182417a3cL, "SetAlias?");
        dump(91, 0x181a72460L, "GetModificationTime?");
        dump(92, 0x182419c00L, "RemoveFile / IsFileCompressed?");
        dump(93, 0x180463a24L, "GetFileSizeOnDisk?");
        dump(100, 0x182418f78L, "FGetSize-by-handle?");
        dump(101, 0x180973294L, "ScanDirectory/recursive find?");

        println("\n========== done ==========");
    }
}
