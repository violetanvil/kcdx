// slot-101 recon, pass 4 — the CONSUMERS (who reaches enumeration via the iterator) and
// the NON-virtual scan method.
//
// Established: CCryPakFindData::vftable (0x183a646b8) has NO scan method — only a dtor +
// a pakPriority-gated handle-cleanup helper. So FindFirst/FindNext is a NON-virtual member.
// The slot-101 factory (0x973294) is reached ONLY virtually (no static call sites). This pass
// finds the real consumers two ways:
//
//  (1) The 0x973xxx CryPak find-handle cluster — decompile the neighbours of the factory
//      (0x973220 cleanup, 0x973285, 0x9738bc dtor, 0x973924) to locate the FindNext scanner
//      that walks entries, and report whether it walks DISK (_findnext64) only or the pak dir.
//  (2) Indirect-call consumers: scan EVERY function for an indirect call through a CCryPak*
//      at +0x328 (the slot-101 binding) — `call qword ptr [rax+0x328]` shapes — so the edge
//      is read in the consumer's body (AP19), not inferred. Report each consumer fn.
//  (3) Decompile FUN_180462af8 (the slot-2 helper's main leaf — likely the actual find/scan
//      worker shared with the iterator) and report its disk-vs-pak markers.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;

public class dump_consumers extends GhidraScript {
    AddressSpace sp; Memory mem; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        println("  name: " + f.getName() + "  size: " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface(); di.openProgram(currentProgram);

        // (1) the 0x973xxx find-handle cluster around the factory.
        println("========== CryPak find-handle cluster (0x973xxx) ==========");
        long[] cluster = {0x180973285L, 0x180973924L, 0x180462af8L};
        for (long v : cluster) decompFull(v, "cluster fn");

        // (2) indirect +0x328 consumers — `CALL qword ptr [reg + 0x328]`.
        println("\n========== indirect +0x328 (slot-101) call sites — read in each consumer body ==========");
        FunctionIterator fns = currentProgram.getListing().getFunctions(true);
        int hits = 0;
        while (fns.hasNext()) {
            Function f = fns.next();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            boolean printed = false;
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!ins.getMnemonicString().toLowerCase().startsWith("call")) continue;
                String s = ins.toString();
                // indirect call through +0x328 (slot 101). Also catch +0x70 (slot 14) for context.
                if (s.contains("0x328")) {
                    if (!printed) { println("  CONSUMER fn " + f.getName() + " @0x" + Long.toHexString(f.getEntryPoint().getOffset())); printed = true; }
                    println("    +0x328 CALL @0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + s);
                    hits++;
                }
            }
        }
        println("  total +0x328 indirect call sites: " + hits);

        // (3) decompile the consumers found (re-scan, decompile each distinct one).
        println("\n========== decompile each +0x328 consumer ==========");
        fns = currentProgram.getListing().getFunctions(true);
        java.util.Set<Long> seen = new java.util.LinkedHashSet<>();
        while (fns.hasNext()) {
            Function f = fns.next();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (ins.getMnemonicString().toLowerCase().startsWith("call") && ins.toString().contains("0x328")) {
                    seen.add(f.getEntryPoint().getOffset()); break;
                }
            }
        }
        int n = 0;
        for (Long c : seen) { if (n++ >= 10) { println("(>10 consumers, stopping)"); break; } decompFull(c, "+0x328 consumer #" + n); }

        di.dispose();
        println("\n===== dump_consumers COMPLETE =====");
    }
}
