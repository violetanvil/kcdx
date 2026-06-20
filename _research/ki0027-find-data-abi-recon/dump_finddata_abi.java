// KI-0027 find-data BUFFER ABI recon (P5 of the file-system-takeover plan).
//
// GOAL: the find-data buffer layout that the CCryPak FindFirst/FindNext triplet
// (slots 63/64/65, vtable +0x1F8/+0x200/+0x208) fills, that the table-glob loader
// FUN_180974484 reads per entry (its local_158 scratch). Need each field at a
// cited byte offset, read from the binary — never inferred.
//
// PRODUCER side (authoritative — what the engine WRITES into the caller buffer):
//   FindFirst  = 0x180973058  (slot 63 / +0x1F8)
//   FindNext   = 0x18041D640  (slot 64 / +0x200)
//   FindClose  = 0x18097383C  (slot 65 / +0x208)  [for completeness]
// CONSUMER side (what the loader EXPECTS): re-decompile FUN_180974484 = 0x180974484,
//   and dump Ghidra's recovered field offsets off local_158.
//
// For each producer body: full decompile + full disassembly, so the STORE sites
// into the caller's buffer (the 3rd arg of FindFirst, the 2nd buffer arg of FindNext)
// are readable at their byte offsets.
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

public class dump_finddata_abi extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    java.io.PrintWriter fw;
    // route output to an explicit file (Ghidra's println goes to a console the
    // detached analyzeHeadless.bat swallows; FileWriter is the capturable sink).
    void println(String s){ super.println(s); if (fw != null){ fw.println(s); fw.flush(); } }
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

    void disasmFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n----- DISASM " + label + " @ 0x" + Long.toHexString(va) + " -----");
        if (f == null) { println("  (no function at this VA)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            println("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ":  " + ins.toString());
        }
    }

    void confirmBinding(long vtVA, long off, long expect, String slot) throws Exception {
        long got = ptr(vtVA + off);
        println("  vtable +0x" + Long.toHexString(off) + " (" + slot + ") = 0x"
            + Long.toHexString(got) + (got == expect ? "  == 0x" + Long.toHexString(expect)
            + " CONFIRMED" : "  != EXPECTED 0x" + Long.toHexString(expect)));
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        fw = new java.io.PrintWriter(new java.io.FileWriter(
            "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0027-find-data-abi-recon\\_producer_dump.txt"));
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long CCRYPAK_VT  = 0x183A95FA8L;
        long FINDFIRST   = 0x180973058L;
        long FINDNEXT    = 0x18041D640L;
        long FINDCLOSE   = 0x18097383CL;
        long CONSUMER    = 0x180974484L;

        println("========== (0) re-confirm the slot 63/64/65 bindings off the live CCryPak vtable ==========");
        confirmBinding(CCRYPAK_VT, 0x1F8, FINDFIRST, "slot 63 FindFirst");
        confirmBinding(CCRYPAK_VT, 0x200, FINDNEXT,  "slot 64 FindNext");
        confirmBinding(CCRYPAK_VT, 0x208, FINDCLOSE, "slot 65 FindClose");

        println("\n========== (1) PRODUCER — FindFirst body (what it writes into the caller buffer) ==========");
        decompFull(FINDFIRST, "FindFirst (slot 63)");
        disasmFull(FINDFIRST, "FindFirst (slot 63)");

        println("\n========== (2) PRODUCER — FindNext body ==========");
        decompFull(FINDNEXT, "FindNext (slot 64)");
        disasmFull(FINDNEXT, "FindNext (slot 64)");

        println("\n========== (3) PRODUCER — FindClose body (for completeness) ==========");
        decompFull(FINDCLOSE, "FindClose (slot 65)");

        println("\n========== (4) CONSUMER — re-decompile FUN_180974484 (field accesses off local_158) ==========");
        decompFull(CONSUMER, "table-glob loader (consumer)");

        di.dispose();
        println("\n===== dump_finddata_abi COMPLETE =====");
        if (fw != null) fw.close();
    }
}
