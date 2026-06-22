// KI-0028 pass 2: the REAL creators downstream of the dispatchers found in pass 1.
//   FUN_1807b18b4 = mfUploadHW's blob->GPU-shader creator (the fn that returns the bool the
//      "Could not create shader" error tests). PRIME create candidate (D3D11 CreateVertex/PixelShader
//      or D3D12 shader-bytecode path).
//   FUN_180bb3aa4 = FUN_180bb315c's per-Graphics-PSO worker.
//   FUN_180bb357c = FUN_180bb2844's per-Compute-PSO worker.
//   FUN_180bb3288 / FUN_180bb3378 / FUN_180bb3ca4 = desc-build helpers (read for the DESC struct).
// Goal: find the device-vtable indirect call (CALL [reg+0xNN] on a device-like object) — the actual
// CreateGraphicsPipelineState (D3D12 slot10 +0x50) / CreatePipelineState (+0xF8) / Create*Shader.
// Dump full body + disasm of each, with INDIRECT-CALL flags. Read-only.
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

public class Ki28PsoCreateDecomp2 extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-cshaderman-pso-consumer-recon\\";

    Address a(long va){ return sp.getAddress(va); }
    void emit(String s){ println(s); if (out != null) out.println(s); }
    void openFile(long va) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_g_" + Long.toHexString(va) + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function at this VA)"); return; }
        emit("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size(bytes): " + f.getBody().getNumAddresses());
        emit("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

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
            Address[] fl = ins.getFlows();
            if (fl != null) for (Address t : fl) {
                Function tf = getFunctionAt(t);
                if (tf == null) tf = getFunctionContaining(t);
                ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                if (tf != null) ann.append(" (").append(tf.getName()).append(")");
            }
            boolean indirect = low.startsWith("call") && mn.contains("[");
            emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann
                + (indirect ? "  <<< INDIRECT-CALL (vtable?)" : ""));
            if (++n > 2000) { emit("  ...[trunc]"); break; }
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] vas = {
            0x1807b18b4L, // mfUploadHW real creator (PRIME)
            0x180bb3aa4L, // Graphics-PSO worker (315c -> here)
            0x180bb357cL, // Compute-PSO worker (2844 -> here)
            0x180bb3288L, // desc-build helper
            0x180bb3378L, // desc-build helper
            0x180bb3ca4L, // desc-build helper (315c)
        };
        String[] labels = {
            "FUN_1807b18b4 mfUploadHW-real-creator (PRIME blob->GPU shader)",
            "FUN_180bb3aa4 Graphics-PSO worker",
            "FUN_180bb357c Compute-PSO worker",
            "FUN_180bb3288 PSO desc helper",
            "FUN_180bb3378 PSO desc helper",
            "FUN_180bb3ca4 PSO desc helper",
        };
        for (int i = 0; i < vas.length; i++) {
            openFile(vas[i]);
            decompFull(vas[i], labels[i]);
            disasmFn(vas[i], labels[i]);
            if (out != null) { out.close(); out = null; }
        }
        emit("\ndone2.");
    }
}
