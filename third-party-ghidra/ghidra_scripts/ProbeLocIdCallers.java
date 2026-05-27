// ProbeLocIdCallers.java -- the int-ID->function BRIDGE probe (§6 full chain).
//
// Question: at the call sites of the by-INT-ID loc getters, is the `id` arg a
// findable CONSTANT (static ID->fn link works) or computed at runtime (it
// doesn't)? Getters are virtually dispatched, so we hunt INDIRECT call sites
// (call qword ptr [reg+disp]) where disp == the getter's vtable offset, then
// inspect the bytes/decompile just before to see how `id` (edx/r8d, the 2nd
// integer arg) is set.
//
// By-ID getter vtable offsets (from DumpLocVtable): slot 1 = 0x8, slot 27 = 0xD8,
// slot 28 = 0xE0. We scan executable memory for `call [reg+disp32/disp8]` with
// those displacements and, for each, decompile the containing function and print
// the lines around the call so we can read whether the id is an immediate.
//
//@category Research

import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class ProbeLocIdCallers extends GhidraScript {

    // vtable offsets of the by-ID getters we care about. NOTE: 0x8 (slot 1) is
    // DELIBERATELY EXCLUDED -- it is vtable slot 1 on every C++ object and
    // over-matches the whole binary's slot-1 virtual calls (the first run's
    // flaw). 0xD8/0xE0 (slots 27/28) are specific enough to be meaningful.
    private static final long[] ID_GETTER_OFFSETS = { 0xD8, 0xE0 };

    private static boolean isIdGetterOffset(long disp) {
        for (long o : ID_GETTER_OFFSETS) if (o == disp) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int callSites = 0, constId = 0, computedId = 0;
        List<String> samples = new ArrayList<>();

        // Walk all instructions; find indirect CALLs through [reg + disp] where
        // disp matches a by-ID getter offset.
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            if (monitor.isCancelled()) break;
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            // indirect call: mnemonic CALL, operand is a dynamic [reg + disp].
            String s = ins.toString();
            if (!s.toUpperCase().startsWith("CALL")) continue;
            if (s.indexOf('[') < 0) continue; // memory-indirect only

            // Extract the displacement from the operand scalars.
            Long disp = null;
            int nOps = ins.getNumOperands();
            for (int op = 0; op < nOps; op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (isIdGetterOffset(v)) disp = v;
                    }
                }
            }
            if (disp == null) continue;

            callSites++;
            Function f = getFunctionContaining(ins.getAddress());
            String fname = f != null ? f.getName() : "<none>";

            // Look back up to ~10 instructions for how the 2nd int arg (EDX/R8D)
            // is loaded: MOV EDX, imm  => constant; otherwise => computed.
            boolean sawConstLoad = false, sawComputed = false;
            Instruction p = ins.getPrevious();
            for (int back = 0; back < 12 && p != null; back++, p = p.getPrevious()) {
                String ps = p.toString().toUpperCase();
                if (ps.startsWith("MOV") && (ps.contains("EDX,") || ps.contains("R8D,"))) {
                    // immediate? operand is a scalar and not a register/mem.
                    boolean imm = ps.matches(".*,\\s*0X[0-9A-F]+$") || ps.matches(".*,\\s*-?\\d+$");
                    if (imm) sawConstLoad = true; else sawComputed = true;
                    break;
                }
                if (ps.startsWith("LEA") && (ps.contains("EDX") || ps.contains("R8D"))) {
                    sawComputed = true; break;
                }
            }
            if (sawConstLoad) constId++;
            else computedId++;

            if (samples.size() < 25) {
                samples.add(String.format("  call[+0x%X] @%s in %s  id=%s",
                        disp, ins.getAddress(), fname,
                        sawConstLoad ? "CONST" : "computed/unknown"));
            }
        }

        di.dispose();
        println("=== by-ID loc-getter indirect call sites ===");
        for (String s : samples) println(s);
        println("");
        println("total by-ID-getter call sites found : " + callSites);
        println("  id loaded as CONSTANT immediate   : " + constId);
        println("  id computed / unknown             : " + computedId);
        println("");
        println("Outcome: CONST-dominant -> static ID->fn link viable (build dump). "
                + "computed-dominant -> static link fails, need call-site capture in the live dump.");
        println("=== done ===");
    }
}
