// file-system-takeover slot-101 recon, pass 2 — the CCryPakFindData iterator itself.
//
// pass 1 established: slot 101 (FUN_180973294) is reached ONLY virtually (3 refs, all
// type=DATA — vtable slot bindings, no static call site). It is a pure factory: it writes
// CCryPakFindData::vftable into the new object. The ENUMERATION lives in the iterator's
// own methods (a FindFirst(name,...) scanner + FindNext). This pass:
//
//  (1) Walk FUN_180973294's instructions, find the LEA that loads CCryPakFindData::vftable
//      (the `*puVar1 = vftable` store source) -> recover the vftable VA.
//  (2) Identify the 3 DATA referrers of 0x973294 — for each, report whether it sits inside
//      a known vtable region (so we know how many ICryPak impls bind this factory).
//  (3) Dump CCryPakFindData::vftable slots; decompile each iterator method. For the scan
//      method specifically, look for: AdjustFileName (a `(**(this+8))` call shape on a
//      CCryPak* — disk-path resolution), _findfirst64/_findnext64 (DISK walk), AND any
//      loaded-pak-vector / FUN_1804631f0 pak-dir-entry walk (UNIFIED — would already see
//      pak entries independent of slot 14).
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
import ghidra.program.model.symbol.ReferenceIterator;
import java.util.LinkedHashSet;
import java.util.Set;

public class dump_finddata extends GhidraScript {
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
        println("  name: " + f.getName() + "  params: " + f.getParameterCount()
            + "  size: " + f.getBody().getNumAddresses());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long FACTORY = 0x180973294L;

        // (1) recover CCryPakFindData::vftable from the factory's instruction stream.
        println("========== recover CCryPakFindData::vftable from factory 0x973294 ==========");
        long fdVT = 0;
        Function fac = getFunctionAt(a(FACTORY));
        if (fac == null) fac = getFunctionContaining(a(FACTORY));
        if (fac != null) {
            InstructionIterator it = currentProgram.getListing().getInstructions(fac.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String s = ins.toString();
                // the vftable load is a LEA reg,[CCryPakFindData::vftable]; print all LEAs + their refs.
                if (ins.getMnemonicString().toLowerCase().startsWith("lea")) {
                    Reference[] rs = ins.getReferencesFrom();
                    StringBuilder b = new StringBuilder();
                    for (Reference r : rs) b.append(" ->0x").append(Long.toHexString(r.getToAddress().getOffset()));
                    println("  LEA @0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + s + b);
                    // heuristic: a vftable lives in .rdata; take the first data ref as candidate.
                    for (Reference r : rs) {
                        long to = r.getToAddress().getOffset();
                        if (fdVT == 0 && to > 0x183000000L && to < 0x186000000L) fdVT = to;
                    }
                }
            }
        }
        println("  CCryPakFindData::vftable candidate = 0x" + Long.toHexString(fdVT));

        // (2) the 3 DATA referrers of the factory — which vtable region each is in.
        println("\n========== DATA referrers of factory 0x973294 (vtable bindings) ==========");
        long CCRYPAK_VT = 0x183A95FA8L;
        Function tgt = getFunctionAt(a(FACTORY));
        ReferenceIterator rit = currentProgram.getReferenceManager().getReferencesTo(tgt.getEntryPoint());
        while (rit.hasNext()) {
            Reference rf = rit.next();
            long from = rf.getFromAddress().getOffset();
            long delta = from - CCRYPAK_VT;
            String note = (from == CCRYPAK_VT + 0x328) ? "  == CCryPak vtable +0x328 (slot 101 binding)"
                : (delta >= 0 && delta < 0x400) ? ("  CCryPak-vt + 0x" + Long.toHexString(delta)) : "  (other vtable/data table)";
            println("  DATA ref from 0x" + Long.toHexString(from) + note);
        }

        // (3) dump the FindData vftable + decompile its methods.
        if (fdVT != 0) {
            println("\n========== CCryPakFindData::vftable @ 0x" + Long.toHexString(fdVT) + " ==========");
            Set<Long> methods = new LinkedHashSet<>();
            for (int i = 0; i < 10; i++) {
                long m = ptr(fdVT + (long) i * 8);
                Function mf = getFunctionAt(a(m));
                if (mf == null) mf = getFunctionContaining(a(m));
                println("  fd slot " + i + " (+0x" + Long.toHexString((long) i * 8) + ") = 0x" + Long.toHexString(m)
                    + (mf != null ? " (" + mf.getName() + ", " + mf.getBody().getNumAddresses() + "b)" : " (no fn — vtable end?)"));
                if (mf != null && m > 0x180000000L && m < 0x183000000L) methods.add(mf.getEntryPoint().getOffset());
            }
            int dm = 0;
            for (Long m : methods) {
                if (dm++ >= 8) { println("\n(>8 methods, stopping)"); break; }
                decompFull(m, "CCryPakFindData method #" + dm);
            }
        } else {
            println("\n  could not recover vftable VA from factory — dumping factory disasm for manual read");
            InstructionIterator it = currentProgram.getListing().getInstructions(fac.getBody(), true);
            while (it.hasNext()) { Instruction ins = it.next(); println("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + ins.toString()); }
        }

        di.dispose();
        println("\n===== dump_finddata COMPLETE =====");
    }
}
