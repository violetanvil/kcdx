// KI-0028 differential trace Step 1c — resolve the SCENE-PASS draw loop (the OPEN gap in
// FINDINGS.md): the per-frame stage that walks compiled render objects and issues the indexed
// draw. Reuse the confirmed named edges:
//   FUN_18086b574 = the per-frame CCRO-compile entry (caller of the compile pass FUN_180429384).
//                   Its body / siblings ARE the render-pass cluster — decompile it + list who
//                   calls IT (the frame-level render entry).
//   FUN_1805025b4 / FUN_1805026c0 = the real DeviceCommandListCommon_D3D12 ops (6 callers each) —
//                   walk their callers upward one level toward the pass loop that records draws.
// Read-only. Decomp + indirect-edge disasm + callers-of, one file per fn.
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

public class Ki28ScenePassTrace extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_sp_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        emit("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses() + " bytes");
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
    }

    void indirectCalls(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n----- INDIRECT (vtable) calls in " + label + " @ 0x" + Long.toHexString(va) + " -----");
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
                String tag = mn.contains("[") ? "  <<< INDIRECT" : "";
                emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + tgt + tag);
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

        openFile("compile_entry_18086b574");
        decompFull(0x18086b574L, "per-frame CCRO-compile entry (render-pass cluster)");
        indirectCalls(0x18086b574L, "compile entry");
        callersOf(0x18086b574L, "compile entry (frame-level render entry)");

        openFile("cmdlist_op_1805025b4");
        decompFull(0x1805025b4L, "DeviceCommandListCommon op A");
        callersOf(0x1805025b4L, "cmdlist op A");

        openFile("cmdlist_op_1805026c0");
        decompFull(0x1805026c0L, "DeviceCommandListCommon op B");
        callersOf(0x1805026c0L, "cmdlist op B");

        if (out != null) { out.close(); out = null; }
        println("Ki28ScenePassTrace: wrote _sp_*.txt to " + OUTDIR);
    }
}
