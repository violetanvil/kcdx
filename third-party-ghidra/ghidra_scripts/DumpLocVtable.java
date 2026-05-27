// DumpLocVtable.java -- resolve the CLocalizedStringsManager vtable address from
// its constructor's LEA, then enumerate the vtable methods + their param shape
// + caller counts. The §6 gating probe (does int-ID link to consumers?).
//
// The ctor FUN_1809f0ce4 does `*this = CLocalizedStringsManager::vftable` via a
// `lea reg, [rip+disp]` whose target IS the vtable. We read the ctor's
// instructions, find the first LEA into rdata, and treat that as the vtable.
//
//@category Research

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpLocVtable extends GhidraScript {

    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();

        // ctor RVA 0x9f0ce4 (FUN_1809f0ce4)
        Address ctor = toAddr(base + 0x9f0ce4L);
        Function f = getFunctionAt(ctor);
        if (f == null) f = getFunctionContaining(ctor);
        println("ctor: " + (f != null ? f.getName() : "?") + " @ " + ctor);

        // Find the first LEA whose target lands in a read-only data block (the
        // vtable). The ctor's very first real op is `*this = vftable`.
        Address vtAddr = null;
        Instruction ins = getInstructionAt(ctor);
        int scanned = 0;
        while (ins != null && scanned < 40) {
            scanned++;
            String mn = ins.getMnemonicString();
            if (mn.equalsIgnoreCase("LEA")) {
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (to != null && to.getOffset() >= base) {
                        println("LEA @ " + ins.getAddress() + " -> " + to
                                + "   (" + ins + ")");
                        if (vtAddr == null) vtAddr = to; // first one = vftable
                    }
                }
            }
            ins = ins.getNext();
            if (ins != null && getFunctionContaining(ins.getAddress()) != f) break;
        }
        if (vtAddr == null) { println("no LEA target found in ctor"); return; }
        println("\nvtable @ " + vtAddr);

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        println("\n=== vtable methods (slot / offset / fn / xrefs / sig) ===");
        for (int slot = 0; slot < 64; slot++) {
            Address slotAddr = vtAddr.add(slot * 8L);
            long target;
            try { target = mem.getLong(slotAddr); } catch (Exception e) { break; }
            target &= 0xFFFFFFFFFFFFFFFFL;
            if (target < base || target > base + 0x10000000L) break;
            Address fnAddr = toAddr(target);
            Function m = getFunctionAt(fnAddr);
            if (m == null) m = getFunctionContaining(fnAddr);
            if (m == null) { println(String.format("slot %2d off 0x%X: %s <no fn>",
                    slot, slot*8, fnAddr)); continue; }
            int callers = 0;
            for (Reference r : getReferencesTo(m.getEntryPoint())) callers++;
            String sig = "?";
            DecompileResults dr = di.decompileFunction(m, 20, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted() && dr.getDecompiledFunction() != null) {
                sig = dr.getDecompiledFunction().getSignature();
                if (sig != null) sig = sig.replaceAll("\\s+"," ").trim();
            }
            println(String.format("slot %2d off 0x%X: %s size=%d xrefs=%d",
                    slot, slot*8, m.getName(), m.getBody().getNumAddresses(), callers));
            println("           " + sig);
        }
        di.dispose();
        println("\n=== done: look for an int/uint-param getter (by-index/ID) vs char*-only (by-key) ===");
    }
}
