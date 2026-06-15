// file-system-takeover step 3.2 prep: dump CCryPak vtable slot 35 (FOpenRaw, +0x118,
// FUN_182418de4) — the one open-family slot whose body is NOT in any prior _research dump,
// so its ABI is unverified. The slot-map reconciliation (slot38=read-raw, slot40=
// FGetCachedFileData) is settled from existing dumps; this fills the one genuine gap.
//
//  (1) Confirm the slot-35 binding from the live vtable @ VA 0x183A95FA8 (+0x118).
//  (2) Decompile slot 35 FUN_182418de4 FULL body + raw disasm -> its ABI (arg count/use) + role.
//  (3) Decompile FUN_1809b2b28 (the open primitive slot 35 + slot 37 share) FULL -> the
//      actual file-open it performs (_wfopen-backed producer per asset-fopen-handle-recon).
//  (4) Confirm slot 37 (+0x128, FUN_182418ec8) is the slot-35 release pair (tail FUN_1809b2b28).
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

public class Slot35FOpenRawDump extends GhidraScript {
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
        // (1) confirm the slot bindings from the live vtable.
        for (int slot : new int[]{35, 36, 37}) {
            long off = VT + (long) slot * 8;
            long fn = ptr(off);
            println("slot " + slot + " (+0x" + Long.toHexString((long) slot * 8) + ") @ vtable+"
                + " = 0x" + Long.toHexString(fn));
        }

        // (2) slot 35 FOpenRaw — the unverified body.
        decompFull(0x182418de4L, "slot35 FOpenRaw (+0x118)");
        disasmFn(0x182418de4L, "slot35 FOpenRaw (+0x118)");

        // (3) the shared open primitive.
        decompFull(0x1809b2b28L, "FUN_1809b2b28 (open primitive, slot35/37 shared)");

        // (4) slot 37 release pair.
        decompFull(0x182418ec8L, "slot37 release-pair (+0x128)");

        di.dispose();
        println("\n===== Slot35FOpenRawDump COMPLETE =====");
    }
}
