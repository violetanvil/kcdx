// KI-0028 differential trace Step 4 — one hop up the indexed-draw path. The trace named the
// first divergence: engine SetIndexBuffer FUN_1805025b4 fires swap-OFF, absent swap-ON. On the
// menu arm the caller was 0x501ebe (in FUN_180501cb0). Decompile FUN_180501cb0 (the SetIndexBuffer
// caller = CDeviceGraphicsCommandInterface::DrawIndexed-class op) + its callers to find the branch
// that decides indexed submission — the branch swap-ON does not take (draw_instanced=19447 but
// ia_set_ib=0). Read-only. Decomp + indirect-edge disasm + callers-of, one file per fn.
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

public class Ki28IndexedCallerDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_hop_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses() + " bytes  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset()));
        DecompileResults r = di.decompileFunction(f, 90, monitor);
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

        // The swap-OFF SetIndexBuffer caller — the indexed-submission op.
        openFile("indexed_caller_180501cb0");
        decompFull(0x180501cb0L, "FUN_180501cb0 = SetIndexBuffer caller (indexed submission op)");
        callsInFn(0x180501cb0L, "FUN_180501cb0");
        callersOf(0x180501cb0L, "FUN_180501cb0 (who decides to call it)");

        // Its direct callers (from Step-1 _render_submit_edges: 0x5029f0 shares the cluster) —
        // one level up is the render pass that chooses indexed vs non-indexed.
        openFile("caller_up_1805029f0");
        decompFull(0x1805029f0L, "FUN_1805029f0 = cluster caller (0x502e89 calls the SetIB op)");
        callsInFn(0x1805029f0L, "FUN_1805029f0");
        callersOf(0x1805029f0L, "FUN_1805029f0");

        if (out != null) { out.close(); out = null; }
        println("Ki28IndexedCallerDecomp: wrote _hop_*.txt to " + OUTDIR);
    }
}
