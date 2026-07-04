// KI-0028 HOP 7c — the frame-driver above the pass dispatcher.
// Chain so far: FUN_18251bb1c -> FUN_1804e8d88 (thin wrapper) -> FUN_180779534 (dispatcher,
// gates pass A on the item list). The item vector [obj+0x298..0x2a0] on obj=[disp.param_1+
// 0x378] is EMPTY swap-ON. FUN_1804e8d88 doesn't fill it, so the fill is earlier — in
// FUN_18251bb1c or a sibling it calls before the dispatch wrapper. Decompile FUN_18251bb1c
// (the render-pass orchestrator) + its callers, and flag every call it makes so the item-
// build sibling is visible. Read-only.
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

public class Ki28Hop7cFrameDriver extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }

    void decompFull(long va, String label) {
        Function f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " (0x" + Long.toHexString(va) + ") =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses() + " bytes");
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
        emit("\n----- CALLS in " + f.getName() + " -----");
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
        emit("\n----- CALLERS of " + f.getName() + " -----");
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
        out = new PrintWriter(new FileWriter(OUTDIR + "_hop7c_frame_driver_18251bb1c.txt"));
        decompFull(0x18251bb1cL, "FUN_18251bb1c = render-pass orchestrator (dispatcher grandparent)");
        if (out != null) { out.close(); out = null; }
        println("Ki28Hop7cFrameDriver: wrote _hop7c_frame_driver_18251bb1c.txt");
    }
}
