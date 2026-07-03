// KI-0028 differential trace Step 1b — decompile the DRAW-RECORD path so the tracer
// instruments REAL adjacent edges (DESIGN.md). Anchors from Ki28RenderSubmitAnchors:
//   FUN_1825381d0 = the "DRAWINDEXEDINSTANCED"-marked D3D12 draw wrapper (leaf just above
//                   the drawcall_probe's slot-13 boundary).
//   FUN_182538c80 = its ONE caller (callsite 0x2538e35) — the engine draw-record DISPATCHER;
//                   this is where the swap-ON path must diverge (draw_indexed=0).
//   FUN_180429794 = CCompiledRenderObject Compile-fail fn; FUN_180429384 = its caller
//                   (3 callsites) — the geometry->PSO submit branch (upstream candidate).
// For each: full decomp + raw disasm flagging INDIRECT (vtable) calls at instruction level
// (the render path dispatches the command list indirectly — direct-callee scans miss it),
// + the callers of the draw dispatcher (the upstream trace edge). Read-only. One file per fn.
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

public class Ki28DrawRecordDecomp extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    Address a(long va) { return sp.getAddress(va); }
    void emit(String s) { if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_dr_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function at this VA)"); return; }
        emit("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses() + " bytes");
        emit("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed: " + (r != null ? r.getErrorMessage() : "null") + ")");
    }

    // Raw disasm flagging INDIRECT calls (vtable dispatch) — the command-list edges the
    // direct-callee scan misses. Also resolves direct call targets to fn names.
    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        Instruction ins = getInstructionAt(f.getEntryPoint());
        int n = 0;
        Address max = f.getBody().getMaxAddress();
        while (ins != null && ins.getAddress().compareTo(max) <= 0) {
            String mn = ins.toString();
            String low = mn.toLowerCase();
            StringBuilder ann = new StringBuilder();
            Address[] fl = ins.getFlows();
            if (fl != null) {
                for (Address t : fl) {
                    Function tf = getFunctionAt(t);
                    if (tf == null) tf = getFunctionContaining(t);
                    ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                    if (tf != null) ann.append(" (").append(tf.getName()).append(")");
                }
            }
            boolean isCall = low.startsWith("call");
            boolean indirect = isCall && mn.contains("[");
            String tag = indirect ? "   <<< INDIRECT-CALL (vtable?)" : (isCall ? "   <call>" : "");
            emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann + tag);
            if (++n > 1600) { emit("  ...[trunc]"); break; }
            ins = ins.getNext();
        }
    }

    void callersOf(long va, String label) {
        emit("\n===== CALLERS OF " + label + " @ 0x" + Long.toHexString(va) + " =====");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator rit = rm.getReferencesTo(a(va));
        int n = 0;
        while (rit.hasNext() && n < 24) {
            Reference ref = rit.next();
            if (!ref.getReferenceType().isCall()) continue;
            Address from = ref.getFromAddress();
            Function cf = getFunctionContaining(from);
            emit("  <- callsite 0x" + Long.toHexString(from.getOffset())
                + (cf != null ? " in " + cf.getName() + " entry=0x"
                    + Long.toHexString(cf.getEntryPoint().getOffset()) : " (no fn)"));
            n++;
        }
        if (n == 0) emit("  (no direct-call callers — reached indirectly / vtable)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // The terminal draw edge + its dispatcher.
        openFile("draw_wrapper_1825381d0");
        decompFull(0x1825381d0L, "DRAWINDEXEDINSTANCED wrapper (D3D12 draw leaf)");
        disasmFn(0x1825381d0L, "DRAWINDEXEDINSTANCED wrapper");
        callersOf(0x1825381d0L, "draw wrapper");

        openFile("draw_dispatcher_182538c80");
        decompFull(0x182538c80L, "draw-record DISPATCHER (caller of the draw wrapper)");
        disasmFn(0x182538c80L, "draw-record DISPATCHER");
        callersOf(0x182538c80L, "draw dispatcher");

        // The compiled-render-object submit branch (upstream geometry->PSO candidate).
        openFile("ccro_compile_180429384");
        decompFull(0x180429384L, "CCompiledRenderObject compile caller (3 submit sites)");
        disasmFn(0x180429384L, "CCRO compile caller");
        callersOf(0x180429384L, "CCRO compile caller");

        openFile("ccro_compilefn_180429794");
        decompFull(0x180429794L, "CCompiledRenderObject Compile fn (PSO-create-fail)");
        disasmFn(0x180429794L, "CCRO Compile fn");

        if (out != null) { out.close(); out = null; }
        println("Ki28DrawRecordDecomp: wrote _dr_*.txt to " + OUTDIR);
    }
}
