// KI-0028 differential trace HOP 7 — the render-item ENQUEUE side.
// HOP 6 proved: pass A's dispatcher FUN_180779534 runs every frame swap-ON but the
// render-item list [obj+0x298..0x2a0] (obj = [dispatcher.param_1 + 0x378], the pass
// object; passA_ctx = obj+0x70) is EMPTY. So the geometry is never enqueued swap-ON.
// This hop finds the append leaf: (1) decompile the dispatcher's caller FUN_1804e8d88
// (owns the pass object + drives the frame — find where obj gets its items), and
// (2) decompile FUN_180501694 (called right after pass A in the dispatcher, on the same
// ctx — likely the list clear/reset, confirms the vector semantics). Read-only.
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

public class Ki28Hop7EnqueueDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_hop7_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses() + " bytes");
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
    }

    void callsInFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n----- CALLS in " + label + " (indirect flagged) -----");
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

        // The dispatcher's caller — owns the pass object [param_1+0x378], drives the frame.
        openFile("dispatcher_caller_1804e8d88");
        decompFull(0x1804e8d88L, "FUN_1804e8d88 = dispatcher caller (owns the pass object)");
        callsInFn(0x1804e8d88L, "FUN_1804e8d88");
        callersOf(0x1804e8d88L, "FUN_1804e8d88");

        // FUN_180501694 — called on passA_ctx right after pass A; likely list clear/reset.
        openFile("list_op_180501694");
        decompFull(0x180501694L, "FUN_180501694 = post-pass-A op on passA_ctx (list clear?)");
        callersOf(0x180501694L, "FUN_180501694");

        if (out != null) { out.close(); out = null; }
        println("Ki28Hop7EnqueueDecomp: wrote _hop7_*.txt to " + OUTDIR);
    }
}
