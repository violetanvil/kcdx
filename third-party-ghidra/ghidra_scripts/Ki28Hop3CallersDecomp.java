// KI-0028 differential trace HOP 3 — the callers of FUN_180501cb0 (the per-item apply+submit
// fn that fires 52764x swap-OFF, 0x swap-ON). HOP 2 falsified the null-IB hypothesis: the whole
// indexed-capable submit path is BYPASSED swap-ON. Decompile the 2 static callers —
// FUN_1804ec3a0 (call site 0x1804ec4b3) and FUN_1805014a0 (call site 0x18050160e) — plus their
// callers, to find which render pass is skipped swap-ON and the branch that gates the call.
// Read-only. Decomp + call list + callers-of, one file per fn.
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

public class Ki28Hop3CallersDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_hop3_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses() + " bytes  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset()));
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
    }

    void callsInFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n----- CALLS in " + label + " @ 0x" + Long.toHexString(va) + " (indirect flagged) -----");
        if (f == null) { emit("  (no function)"); return; }
        Instruction ins = getInstructionAt(f.getEntryPoint());
        Address max = f.getBody().getMaxAddress();
        while (ins != null && ins.getAddress().compareTo(max) <= 0) {
            String mn = ins.toString();
            if (mn.toLowerCase().startsWith("call")) {
                Address[] fl = ins.getFlows();
                String tgt = "";
                if (fl != null) for (Address t : fl) {
                    Function tf = getFunctionAt(t);
                    tgt += " -> 0x" + Long.toHexString(t.getOffset())
                         + (tf != null ? "(" + tf.getName() + ")" : "");
                }
                emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + tgt
                    + (mn.contains("[") ? "  <<< INDIRECT" : ""));
            }
            ins = ins.getNext();
        }
    }

    void callersOf(long va, String label) {
        emit("\n----- CALLERS OF " + label + " @ 0x" + Long.toHexString(va) + " -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator rit = rm.getReferencesTo(a(va));
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

        // Caller A — call site 0x1804ec4b3.
        openFile("caller_a_1804ec3a0");
        decompFull(0x1804ec3a0L, "FUN_1804ec3a0 = caller A of the apply+submit fn");
        callsInFn(0x1804ec3a0L, "FUN_1804ec3a0");
        callersOf(0x1804ec3a0L, "FUN_1804ec3a0");

        // Caller B — call site 0x18050160e.
        openFile("caller_b_1805014a0");
        decompFull(0x1805014a0L, "FUN_1805014a0 = caller B of the apply+submit fn");
        callsInFn(0x1805014a0L, "FUN_1805014a0");
        callersOf(0x1805014a0L, "FUN_1805014a0");

        if (out != null) { out.close(); out = null; }
        println("Ki28Hop3CallersDecomp: wrote _hop3_*.txt to " + OUTDIR);
    }
}
