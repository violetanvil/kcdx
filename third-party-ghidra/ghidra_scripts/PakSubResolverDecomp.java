// Phase 8.5 sub-resolver: FOpen (CCryPak slot 36, 0x1804614A0) calls [rax+8] at 0x1804615FA,
// where rax = [r12] = CCryPak's own vtable (r12 = FOpen's `this`). So the call is CCryPak
// vtable SLOT 1 (+0x8), same `this`, path in rdx, computed flags in r9d.
//
// CCryPak (== ICryPak) vtable: VA 0x183A95FA8 (RVA 0x03A95FA8).
//
// This script:
//  (1) Read vtable slots 0..5 (+0x0..+0x28) raw pointers from 0x183A95FA8 -> resolve slot1 target.
//  (2) Decompile slot1 target (the [rax+8] sub-resolver) FULL body.
//  (3) Dump a wide raw disassembly of the slot1 target so flag tests (0x10000/0x4/0x2/0x10006)
//      and path/root handling are visible at the instruction level (AP2 - cite the line).
//  (4) If slot1 tail-calls / dispatches one level deeper into the real loose-vs-pak logic,
//      decompile that next function too.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;

public class PakSubResolverDecomp extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount());
        println("  entry: 0x" + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) {
            println(r.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
        }
    }

    // raw disassembly of a function body (only instructions WITHIN the function's body
    // address-set, so it never spills into adjacent functions) with flow-target annotations.
    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        int n = 0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mn = ins.toString();
            Address[] fl = ins.getFlows();
            StringBuilder ann = new StringBuilder();
            if (fl != null) {
                for (Address t : fl) {
                    Function tf = getFunctionAt(t);
                    if (tf == null) tf = getFunctionContaining(t);
                    ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                    if (tf != null) ann.append(" (").append(tf.getName()).append(")");
                }
            }
            println("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + mn + ann);
            if (++n > 900) { println("  ...[trunc disasm]"); break; }
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long vt = 0x183A95FA8L; // CCryPak vtable VA
        println("CCryPak vtable @ 0x" + Long.toHexString(vt));
        long[] slots = new long[8];
        for (int i = 0; i < 8; i++) {
            long target = ptr(vt + 8L*i);
            slots[i] = target;
            Function tf = getFunctionAt(a(target));
            if (tf == null) tf = getFunctionContaining(a(target));
            println("  slot " + i + " (+0x" + Long.toHexString(8L*i) + ") = 0x"
                + Long.toHexString(target) + (tf!=null? "  fn="+tf.getName() : "  (no fn)"));
        }

        long slot1 = slots[1];
        println("\n[*] FOpen's [rax+8] sub-resolver = CCryPak vtable slot 1 = 0x" + Long.toHexString(slot1));

        // (2) + (3) the sub-resolver
        decompFull(slot1, "CCryPak slot1 sub-resolver (search-path precedence loop)");
        disasmFn(slot1, "CCryPak slot1 sub-resolver");

        // (4) the leaf opener (called at the loop body + the fallthrough) - where the
        //     flag tests (0x10000 / 0x4 / 0x2 / 0x10006) and pak-vs-loose live.
        decompFull(0x1804621bcL, "FUN_1804621bc leaf opener (pak-or-loose, flag tests)");
        disasmFn(0x1804621bcL, "FUN_1804621bc leaf opener");

        // (5) the per-source-mode existence checks
        decompFull(0x1804631f0L, "FUN_1804631f0 (PAK-membership existence check)");
        decompFull(0x1819c9cb4L, "FUN_1819c9cb4 (DISK/loose existence check?)");
        decompFull(0x18241ad60L, "FUN_18241ad60 (pakPriority==3 mode check?)");
        // FUN_1804631f0's per-source open helper that maps a found path -> handle:
        decompFull(0x180462664L, "FUN_180462664 (called by AdjustFileName w/ local_848)");

        // Resolve referenced global data + a few struct field roots, as strings where possible.
        println("\n===== GLOBAL DATA =====");
        long[] globals = { 0x184927274L, 0x184927278L, 0x18549b490L };
        for (long g : globals) {
            try {
                int v = mem.getInt(a(g));
                println("  [0x" + Long.toHexString(g) + "] (int) = 0x" + Integer.toHexString(v) + " (" + v + ")");
            } catch (Exception e) { println("  [0x" + Long.toHexString(g) + "] = <unreadable>"); }
        }

        println("\ndone.");
    }
}
