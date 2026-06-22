// KI-0028 pass 3: the device-call LEAVES.
//   FUN_180b209fc = mfUploadHW chain leaf (FUN_1807b18b4 -> here): blob{ptr,len} -> GPU shader object.
//      Device-create wrapper on global render device DAT_1852b8578. Find Create*Shader / device vtable call.
//   FUN_180bb42c8 = Graphics-PSO chain leaf (FUN_180bb3aa4 -> here): per-PSO build+submit.
//      Find CreateGraphicsPipelineState (D3D12 slot10 +0x50) / desc build + device vtable call.
// Follow ONE level deeper automatically: for each, also dump any direct-callee whose body contains an
// indirect call with a large displacement (>= 0x40), which is the device-vtable-slot signature.
// Read-only.
//@category KCD2

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class Ki28PsoCreateDecomp3 extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-cshaderman-pso-consumer-recon\\";

    Address a(long va){ return sp.getAddress(va); }
    void emit(String s){ println(s); if (out != null) out.println(s); }
    void openFile(long va) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_h_" + Long.toHexString(va) + ".txt"));
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
        else emit("  (decompile failed)");
    }

    // disasm; collect direct-call targets; flag indirect calls and report their displacement.
    LinkedHashSet<Long> disasmFn(long va, String label) {
        LinkedHashSet<Long> callees = new LinkedHashSet<>();
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no function)"); return callees; }
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
                if (low.startsWith("call") && tf != null) callees.add(tf.getEntryPoint().getOffset());
            }
            boolean indirect = low.startsWith("call") && mn.contains("[");
            emit("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann
                + (indirect ? "  <<< INDIRECT-CALL (vtable?)" : ""));
            if (++n > 2500) { emit("  ...[trunc]"); break; }
        }
        return callees;
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] vas = { 0x180b209fcL, 0x180bb42c8L };
        String[] labels = {
            "FUN_180b209fc shader-create LEAF (mfUploadHW chain) — Create*Shader candidate",
            "FUN_180bb42c8 PSO-create LEAF (Graphics-PSO chain) — CreateGraphicsPipelineState candidate",
        };
        LinkedHashSet<Long> deeper = new LinkedHashSet<>();
        for (int i = 0; i < vas.length; i++) {
            openFile(vas[i]);
            decompFull(vas[i], labels[i]);
            LinkedHashSet<Long> callees = disasmFn(vas[i], labels[i]);
            if (out != null) { out.close(); out = null; }
            deeper.addAll(callees);
        }

        // Auto-follow one level: decompile each direct callee whose disasm contains a high-disp
        // indirect call (device-vtable-slot signature). Keep it bounded.
        emit("\n========== ONE LEVEL DEEPER (callees with high-disp indirect calls) ==========");
        int dn = 0;
        for (Long cva : deeper) {
            if (dn >= 14) { emit("  ...[more callees omitted]"); break; }
            Function f = getFunctionAt(a(cva));
            if (f == null) continue;
            if (f.getBody().getNumAddresses() > 1200) continue; // skip huge generic helpers
            // scan for an indirect call with displacement >= 0x40
            boolean hasDeviceCall = false;
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String mn = ins.toString();
                if (mn.toLowerCase().startsWith("call") && mn.contains("[") && mn.contains("+ 0x")) {
                    // crude disp extract
                    int idx = mn.indexOf("+ 0x");
                    String hex = mn.substring(idx + 4).replaceAll("[^0-9a-fA-F].*$", "");
                    try { if (Long.parseLong(hex, 16) >= 0x40) { hasDeviceCall = true; break; } }
                    catch (Exception e) {}
                }
            }
            if (!hasDeviceCall) continue;
            dn++;
            openFile(cva);
            decompFull(cva, "DEEPER device-call candidate FUN_" + Long.toHexString(cva));
            disasmFn(cva, "DEEPER FUN_" + Long.toHexString(cva));
            if (out != null) { out.close(); out = null; }
        }

        emit("\ndone3.");
    }
}
