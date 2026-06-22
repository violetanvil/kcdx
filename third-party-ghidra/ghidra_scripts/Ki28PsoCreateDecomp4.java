// KI-0028 pass 4: the actual D3D12 CreateGraphicsPipelineState site.
// FUN_180bb42c8 branches to FUN_180bb42fc (sync) or FUN_180bb44b0 (deferred). One of these builds
// a D3D12_GRAPHICS_PIPELINE_STATE_DESC and calls ID3D12Device::CreateGraphicsPipelineState
// (vtable slot 10, +0x50) OR CreatePipelineState (+0xF8). Decompile both + their immediate callees
// that carry a high-disp indirect call. Engine confirmed NCryDX12 (D3D12) in pass 3.
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

public class Ki28PsoCreateDecomp4 extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-cshaderman-pso-consumer-recon\\";

    Address a(long va){ return sp.getAddress(va); }
    void emit(String s){ println(s); if (out != null) out.println(s); }
    void openFile(String tag) throws Exception {
        if (out != null) out.close();
        out = new PrintWriter(new FileWriter(OUTDIR + "_i_" + tag + ".txt"));
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no fn)"); return; }
        emit("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size(bytes): " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) emit(r.getDecompiledFunction().getC());
        else emit("  (decompile failed)");
    }

    LinkedHashSet<Long> disasmFn(long va, String label) {
        LinkedHashSet<Long> callees = new LinkedHashSet<>();
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        emit("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { emit("  (no fn)"); return callees; }
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
            if (++n > 3000) { emit("  ...[trunc]"); break; }
        }
        return callees;
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] vas = { 0x180bb42fcL, 0x180bb44b0L };
        String[] labels = { "FUN_180bb42fc PSO-create SYNC", "FUN_180bb44b0 PSO-create DEFERRED" };
        LinkedHashSet<Long> deeper = new LinkedHashSet<>();
        for (int i = 0; i < vas.length; i++) {
            openFile(Long.toHexString(vas[i]));
            decompFull(vas[i], labels[i]);
            deeper.addAll(disasmFn(vas[i], labels[i]));
            if (out != null) { out.close(); out = null; }
        }

        // follow callees that hold a high-disp indirect call (the device-vtable signature)
        int dn = 0;
        for (Long cva : deeper) {
            if (dn >= 20) break;
            Function f = getFunctionAt(a(cva));
            if (f == null || f.getBody().getNumAddresses() > 2200) continue;
            boolean dev = false;
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String mn = ins.toString();
                if (mn.toLowerCase().startsWith("call") && mn.contains("[") && mn.contains("+ 0x")) {
                    int idx = mn.indexOf("+ 0x");
                    String hex = mn.substring(idx + 4).replaceAll("[^0-9a-fA-F].*$", "");
                    try { long d = Long.parseLong(hex, 16); if (d >= 0x40 && d <= 0x400) { dev = true; break; } }
                    catch (Exception e) {}
                }
            }
            if (!dev) continue;
            dn++;
            openFile("deep_" + Long.toHexString(cva));
            decompFull(cva, "DEEPER device-call FUN_" + Long.toHexString(cva));
            disasmFn(cva, "DEEPER FUN_" + Long.toHexString(cva));
            if (out != null) { out.close(); out = null; }
        }
        emit("\ndone4. deeper-emitted=" + dn);
    }
}
