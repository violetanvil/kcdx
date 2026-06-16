// file-system-takeover step 3.2 sub-step 1: dump CCryPak vtable slot 66 (+0x210,
// FUN_18241A3BC) — the one flipped read-family slot whose body is in NO prior _research
// dump, so its member-call ABI is UNVERIFIED. front1 marks it `i` (inferred-only,
// a _fileno-shaped variant). The cutover flips it THUNK->KCDX, so the kcdx impl must
// match its exact dispatch ABI (AP2/AP19 — a wrong sig is called with garbage args, a
// green build never catches it). This dump reads the ABI off the decompiled body, never
// from the name or the front1 fingerprint (results-driven.md / AP2).
//
//  (1) Confirm the slot-66 binding from the live vtable @ VA 0x183A95FA8 (+0x210);
//      assert it points at 0x18241A3BC.
//  (2) Decompile slot 66 FUN_18241A3BC FULL body + raw disasm -> arg count/types,
//      return type, role, and whether it handle-tag dispatches like the read family.
//  (3) Decompile each leaf it calls (front1 says a _fileno-shaped call) -> what the
//      body actually does with the handle.
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
import java.util.LinkedHashSet;
import java.util.Set;

public class dump_slot66 extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    // collect the call targets of a function so we can decompile its leaves.
    Set<Long> callTargets(long va) {
        Set<Long> out = new LinkedHashSet<>();
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        if (f == null) return out;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String mn = ins.getMnemonicString();
            if (mn != null && mn.toLowerCase().startsWith("call")) {
                Address[] fl = ins.getFlows();
                if (fl != null) for (Address t : fl) out.add(t.getOffset());
            }
        }
        return out;
    }

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

    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
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
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long VT = 0x183A95FA8L; // CCryPak vtable VA (RTTI .?AVCCryPak@@)
        long SLOT66 = 0x18241A3BCL; // expected FUN at +0x210

        // (1) confirm the slot-66 binding from the live vtable.
        long off = VT + 66L * 8;        // +0x210
        long fn = ptr(off);
        println("slot 66 (+0x" + Long.toHexString(66L * 8) + ") @ vtable = 0x" + Long.toHexString(fn));
        println("expected                                   = 0x" + Long.toHexString(SLOT66));
        println("BINDING " + (fn == SLOT66 ? "CONFIRMED (matches expected)" : "MISMATCH — slot 66 does NOT point at expected FUN"));
        // also print neighbours for sanity (slot 65 / 67).
        println("slot 65 (+0x" + Long.toHexString(65L * 8) + ") = 0x" + Long.toHexString(ptr(VT + 65L * 8)));
        println("slot 67 (+0x" + Long.toHexString(67L * 8) + ") = 0x" + Long.toHexString(ptr(VT + 67L * 8)));

        // (2) slot 66 body — the unverified member ABI.
        decompFull(SLOT66, "slot66 (+0x210)");
        disasmFn(SLOT66, "slot66 (+0x210)");

        // (3) every leaf slot 66 calls — decompile each to see what it does with the handle.
        Set<Long> leaves = callTargets(SLOT66);
        println("\n===== slot66 CALL TARGETS (" + leaves.size() + ") =====");
        for (Long t : leaves) {
            Function tf = getFunctionAt(a(t));
            if (tf == null) tf = getFunctionContaining(a(t));
            println("  leaf 0x" + Long.toHexString(t) + (tf != null ? " (" + tf.getName() + ")" : " (no fn)"));
        }
        int n = 0;
        for (Long t : leaves) {
            if (n++ >= 6) { println("\n(more than 6 leaves — stopping leaf decomp)"); break; }
            decompFull(t, "slot66 leaf #" + n);
        }

        di.dispose();
        println("\n===== dump_slot66 COMPLETE =====");
    }
}
