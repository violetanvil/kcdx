// KI-0028 PSO/shader-object CREATE end recon. Image base 0x180000000. Read-only.
// Targets (the blob -> GPU-shader-object / PSO create site, downstream of the cache-read gate):
//   1. FUN_18252f3ac = CHWShader_D3D::mfUploadHW (pinned by "Could not create shader '%s'(0x%llx)"
//      xref @ 0x18252f423). THE PRIME TARGET: blob -> GPU shader object. Read full body + ABI +
//      device-vtable indirect-call edges + 1-2 callers' call sites.
//   2. PipelineStateCacheManager.cpp fns: FUN_180bb315c, FUN_180bb2844, FUN_180bb2ad8, FUN_180bb23c0.
//      Which one calls device CreateGraphicsPipelineState / CreatePipelineState (builds a
//      D3D12_GRAPHICS_PIPELINE_STATE_DESC and submits via the device vtable)?
// Method: decompile each full body; raw-disasm each to surface indirect [reg+0xNN] calls at the
// instruction level (so a device-vtable slot is READ, cited by site, never inferred); list call
// edges (direct targets resolved to fn name). Dump each to _research/...-recon/_f<n>_<addr>.txt.
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
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;

public class Ki28PsoCreateDecomp extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-cshaderman-pso-consumer-recon\\";

    Address a(long va){ return sp.getAddress(va); }

    void emit(String s){ println(s); if (out != null) out.println(s); }

    void openFile(long va) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_f_" + Long.toHexString(va) + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function at this VA)"); return; }
        emit("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount());
        emit("  entry: 0x" + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size(bytes): " + f.getBody().getNumAddresses());
        emit("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) {
            emit(r.getDecompiledFunction().getC());
        } else {
            emit("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
        }
    }

    // Raw disassembly of a function body. Annotates EVERY indirect call (CALL [reg+disp])
    // and direct-call flow targets so device-vtable slots are visible at the instruction level.
    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mn = ins.toString();
            String low = mn.toLowerCase();
            StringBuilder ann = new StringBuilder();
            // direct flow targets -> fn name
            Address[] fl = ins.getFlows();
            if (fl != null) {
                for (Address t : fl) {
                    Function tf = getFunctionAt(t);
                    if (tf == null) tf = getFunctionContaining(t);
                    ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                    if (tf != null) ann.append(" (").append(tf.getName()).append(")");
                }
            }
            // flag indirect calls (vtable dispatch) loudly
            boolean isCall = low.startsWith("call");
            boolean indirect = isCall && mn.contains("[");
            String tag = indirect ? "  <<< INDIRECT-CALL (vtable?)" : "";
            emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann + tag);
            if (++n > 1400) { emit("  ...[trunc disasm]"); break; }
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long[][] targets = {
            {0x18252f3acL, 1}, // mfUploadHW PRIME
            {0x180bb315cL, 2},
            {0x180bb2844L, 2},
            {0x180bb2ad8L, 2},
            {0x180bb23c0L, 2},
        };
        String[] labels = {
            "FUN_18252f3ac CHWShader_D3D::mfUploadHW (PRIME — blob->GPU shader obj)",
            "FUN_180bb315c PipelineStateCacheManager",
            "FUN_180bb2844 PipelineStateCacheManager",
            "FUN_180bb2ad8 PipelineStateCacheManager",
            "FUN_180bb23c0 PipelineStateCacheManager",
        };

        for (int i = 0; i < targets.length; i++) {
            long va = targets[i][0];
            openFile(va);
            decompFull(va, labels[i]);
            disasmFn(va, labels[i]);
        }
        if (out != null) { out.close(); out = null; }

        // Callers of mfUploadHW — read the call site (1-2). Resolve via references-to.
        emit("\n========== CALLERS OF mfUploadHW (0x18252f3ac) ==========");
        ghidra.program.model.symbol.ReferenceManager rm = currentProgram.getReferenceManager();
        ghidra.program.model.symbol.ReferenceIterator rit = rm.getReferencesTo(a(0x18252f3acL));
        java.util.LinkedHashSet<Long> callerFns = new java.util.LinkedHashSet<>();
        while (rit.hasNext()) {
            ghidra.program.model.symbol.Reference ref = rit.next();
            Address from = ref.getFromAddress();
            Function cf = getFunctionContaining(from);
            emit("  ref from 0x" + Long.toHexString(from.getOffset())
                + (cf != null ? " in " + cf.getName() + " @0x" + Long.toHexString(cf.getEntryPoint().getOffset()) : " (no fn)")
                + "  type=" + ref.getReferenceType());
            if (cf != null) callerFns.add(cf.getEntryPoint().getOffset());
        }
        int cn = 0;
        for (Long cva : callerFns) {
            if (cn++ >= 2) { emit("  ...[more callers omitted]"); break; }
            openFile(cva);
            decompFull(cva, "CALLER of mfUploadHW");
            disasmFn(cva, "CALLER of mfUploadHW");
            if (out != null) { out.close(); out = null; }
        }

        emit("\ndone.");
    }
}
