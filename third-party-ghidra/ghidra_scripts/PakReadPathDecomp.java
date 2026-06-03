// FRONT 3 (Phase 8.5 pak-resolver): the HANDLE-CONSUME / read path.
// After CCryPak::FOpen (slot 36, 0x1804614A0) returns a handle, a handle-consumed asset
// (.lua/.xml/scripts) is READ through that handle via the FRead-family machinery.
// FWrite (slot 41, +0x148, FUN_180a700c8) + FClose (slot 55, +0x1B8, FUN_1804609d0) already
// showed (PakFOpenConfirm _fopen_confirm.txt) a pak-vs-OS dispatch: a handle index computed
// from the FILE* ptr is compared against the [this+0x40]..[this+0x48] array bound (0x18 stride)
// -> in-bound = pak path; out-of-bound = the real CRT fwrite/fclose on a genuine FILE*.
//
// This script maps the READ side of that same dispatch + the shared CCryFile helper:
//  (1) Dump CCryPak vtable read-family slots (+0x118..+0x150) to find FRead + neighbors.
//  (2) Decompile FRead (the read method on the handle) FULL body + raw disasm -> the pak/OS branch.
//  (3) Decompile FUN_1804605bc (the shared CCryFile open-at-0x10006 helper cited in seed id 136
//      ModManager_ReadModOrder) FULL + disasm -> how a handle is OBTAINED + wrapped for reading.
//  (4) Decompile FUN_1804613d0 (top-of-FWrite/FClose helper, the suspected handle->pak-entry
//      resolver/lock) + FUN_180462014 + FUN_180506f94 (the pak-arm read/lock leaves).
//  (5) Confirm FGetSize / FSeek / FEof neighbors share the same dispatch (the read consume API).
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
import ghidra.program.model.symbol.Reference;

public class PakReadPathDecomp extends GhidraScript {
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
            if (++n > 700) { println("  ...[trunc disasm]"); break; }
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
        // (1) read-family slots: FRead is expected near FWrite(slot 41,+0x148). Dump 34..56.
        for (int i = 34; i <= 56; i++) {
            long target = ptr(vt + 8L*i);
            Function tf = getFunctionAt(a(target));
            if (tf == null) tf = getFunctionContaining(a(target));
            println("  slot " + i + " (+0x" + Long.toHexString(8L*i) + ") = 0x"
                + Long.toHexString(target) + (tf!=null? "  fn="+tf.getName() : "  (no fn)"));
        }

        // (2) FRead candidates: slot 40 (+0x140) just below FWrite, and slot 37/38/39 (read/seek).
        long sFRead = ptr(vt + 0x140L);  // slot 40
        decompFull(sFRead, "slot40 (+0x140) CLAIM: FRead");
        disasmFn(sFRead, "slot40 (+0x140) CLAIM: FRead");

        // neighbors that consume a handle (FGetSize/FSeek/FTell/FEof/FGetc) — confirm the dispatch
        long sSeek = ptr(vt + 0x130L);   // slot 38
        long sTell = ptr(vt + 0x138L);   // slot 39
        long sGetSize = ptr(vt + 0x128L);// slot 37
        decompFull(sGetSize, "slot37 (+0x128) read-consume neighbor (FGetSize?)");
        decompFull(sSeek, "slot38 (+0x130) read-consume neighbor (FSeek?)");
        decompFull(sTell, "slot39 (+0x138) read-consume neighbor (FTell/FEof?)");

        // (3) the shared CCryFile open-at-0x10006 helper cited in seed id 136
        decompFull(0x1804605bcL, "FUN_1804605bc — shared CCryFile open helper (0x10006), seed id 136");
        disasmFn(0x1804605bcL, "FUN_1804605bc — shared CCryFile open helper");
        // its callers (who consumes the handle this returns)
        println("\n===== callers of FUN_1804605bc (CCryFile helper) =====");
        Reference[] refs = getReferencesTo(a(0x1804605bcL));
        int shown = 0;
        for (Reference rf : refs) {
            Function cf = getFunctionContaining(rf.getFromAddress());
            println("  ref from 0x" + Long.toHexString(rf.getFromAddress().getOffset())
                + " type=" + rf.getReferenceType() + " in " + (cf!=null?cf.getName():"?"));
            if (++shown >= 25) break;
        }

        // (4) the handle->pak-entry resolver + pak-arm read/lock leaves (from FWrite/FClose body)
        decompFull(0x1804613d0L, "FUN_1804613d0 — top-of-FWrite/FClose helper (handle->pak-entry lock?)");
        decompFull(0x180462014L, "FUN_180462014 — predicate in FWrite (pak-handle test?)");
        decompFull(0x180506f94L, "FUN_180506f94 — pak-arm leaf in FWrite");
        decompFull(0x1804613fcL, "FUN_1804613fc — tail-of-FWrite/FClose helper (unlock?)");

        println("\ndone.");
    }
}
