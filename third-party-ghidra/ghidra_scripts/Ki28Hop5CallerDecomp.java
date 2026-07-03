// KI-0028 differential trace HOP 5 — the LIVE (runtime-captured) callers of pass A/B.
// HOP-4 _ReturnAddress capture (decimal mod_off from the log, converted CAREFULLY):
// pass A's sole live caller site is ret-addr 7837037 = 0x77956d (VA 0x18077956d);
// pass B's is 8813960 = 0x867d88 (VA 0x180867d88). (First attempt mis-converted to
// 0x779a6d/0x867e88 — landed mid-instruction in an unrelated float fn; the missing
// CALL in the site context was the tell.) Decompile the containing functions + the
// call-site instructions + their callers. Read-only.
//@category KCD2

import java.io.FileWriter;
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class Ki28Hop5CallerDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_hop5_" + tag + ".txt"));
    }

    void siteContext(long retVa, String label) {
        emit("\n----- CALL-SITE CONTEXT for ret-addr 0x" + Long.toHexString(retVa)
            + " (" + label + ") -----");
        // Walk back a few instructions to show the call itself.
        Instruction ins = getInstructionContaining(a(retVa - 1));
        for (int back = 0; ins != null && back < 6; ++back) ins = ins.getPrevious();
        for (int i = 0; ins != null && i < 10; ++i) {
            emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + ins);
            ins = ins.getNext();
            if (ins != null && ins.getAddress().getOffset() > retVa + 0x10) break;
        }
    }

    void decompContaining(long va, String label) {
        Function f = getFunctionContaining(a(va));
        emit("\n===== DECOMP fn CONTAINING 0x" + Long.toHexString(va) + " (" + label + ") =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses() + " bytes");
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
        // Its callers.
        emit("\n----- CALLERS OF " + f.getName() + " @ 0x"
            + Long.toHexString(f.getEntryPoint().getOffset()) + " -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator rit = rm.getReferencesTo(f.getEntryPoint());
        int n = 0;
        while (rit.hasNext() && n < 24) {
            Reference ref = rit.next();
            if (!ref.getReferenceType().isCall()) continue;
            Function cf = getFunctionContaining(ref.getFromAddress());
            emit("  <- 0x" + Long.toHexString(ref.getFromAddress().getOffset())
                + (cf != null ? " in " + cf.getName() + " entry=0x"
                    + Long.toHexString(cf.getEntryPoint().getOffset()) : ""));
            n++;
        }
        if (n == 0) emit("  (no direct callers — indirect/vtable)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // Pass A's live caller (THE key one — pass A drives all engine draws).
        openFile("live_caller_a_18077956d");
        siteContext(0x18077956dL, "pass A live call site");
        decompContaining(0x18077956dL, "pass A's live caller");

        // Pass B's live caller (secondary).
        openFile("live_caller_b_180867d88");
        siteContext(0x180867d88L, "pass B live call site");
        decompContaining(0x180867d88L, "pass B's live caller");

        if (out != null) { out.close(); out = null; }
        println("Ki28Hop5CallerDecomp: wrote _hop5_*.txt to " + OUTDIR);
    }
}
