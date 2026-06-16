// file-system-takeover slot-101 callers recon — does any engine consumer reach directory
// enumeration via slot 101 (FindFirst / CCryPakFindData iterator), such that THUNK-101 /
// KCDX-14 would disagree for the same dir?
//
// slot 101 = FUN_180973294 (RVA 0x973294, +0x328) — front1 confirmed it is the
// CCryPakFindData FACTORY only (allocs 0x20, assigns CCryPakFindData::vftable, inits a
// circular find-handle node, sentinel 0x101, returns the empty iterator). It does NOT
// enumerate — no AdjustFileName, no _findfirst64. The ACTUAL scan is a method on
// CCryPakFindData::vftable (FindFirst/FindNext), never read by a prior dump. This script:
//
//  (1) Confirm slot-101 binding from the live vtable @ VA 0x183A95FA8 (+0x328) == 0x973294.
//  (2) Find every CALLER of FUN_180973294 (reverse refs), and for each read the caller's
//      decompiled body so the call edge is READ in the caller (AP19), not inferred.
//  (3) Read CCryPakFindData::vftable: dump its slots, decompile the iterator methods
//      (the FindFirst(name,...) scanner + FindNext), and look for AdjustFileName(slot1
//      shape) / _findfirst64 (disk) AND any pak-directory walk (the loaded-pak vector
//      / FUN_1804631f0 pak-dir-entry family) — disk-only vs unified.
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
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolIterator;
import java.util.LinkedHashSet;
import java.util.Set;

public class dump_slot101 extends GhidraScript {
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
            + "  params: " + f.getParameterCount() + "  size: " + f.getBody().getNumAddresses());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) {
            println(r.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long VT = 0x183A95FA8L;       // CCryPak vtable VA (RTTI .?AVCCryPak@@)
        long SLOT101 = 0x180973294L;  // expected FUN at +0x328 (FindFirst factory)

        // (1) confirm slot-101 binding.
        long off = VT + 101L * 8;     // +0x328
        long fn = ptr(off);
        println("slot 101 (+0x" + Long.toHexString(101L * 8) + ") @ vtable = 0x" + Long.toHexString(fn));
        println("expected                                    = 0x" + Long.toHexString(SLOT101));
        println("BINDING " + (fn == SLOT101 ? "CONFIRMED" : "MISMATCH"));
        println("slot 100 (+0x320) = 0x" + Long.toHexString(ptr(VT + 100L * 8)));
        println("slot 14  (+0x70)  = 0x" + Long.toHexString(ptr(VT + 14L * 8)) + "  (ForEachFile, KCDX in takeover)");

        // (2) callers of FUN_180973294 — read in each caller's body (AP19).
        println("\n========== CALLERS OF slot-101 factory 0x973294 ==========");
        Function tgt = getFunctionAt(a(SLOT101));
        if (tgt == null) tgt = getFunctionContaining(a(SLOT101));
        Set<Long> callerEntries = new LinkedHashSet<>();
        if (tgt != null) {
            ReferenceIterator rit = currentProgram.getReferenceManager().getReferencesTo(tgt.getEntryPoint());
            int nref = 0;
            while (rit.hasNext()) {
                Reference rf = rit.next();
                Address from = rf.getFromAddress();
                String rtype = rf.getReferenceType().toString();
                Function cf = getFunctionContaining(from);
                long centry = (cf != null) ? cf.getEntryPoint().getOffset() : -1;
                println("  ref #" + (++nref) + " from 0x" + Long.toHexString(from.getOffset())
                    + " type=" + rtype
                    + (cf != null ? "  in " + cf.getName() + " @0x" + Long.toHexString(centry) : "  (no containing fn)"));
                if (cf != null && rtype.toLowerCase().contains("call")) callerEntries.add(centry);
            }
            println("  total refs: " + nref + "   distinct CALL-site caller fns: " + callerEntries.size());
        } else {
            println("  (no function at 0x973294)");
        }
        // decompile each distinct caller so the edge is READ in the caller body.
        int n = 0;
        for (Long ce : callerEntries) {
            decompFull(ce, "CALLER #" + (++n) + " of slot-101");
        }

        // (3) CCryPakFindData::vftable — the iterator's own methods.
        println("\n========== CCryPakFindData::vftable ==========");
        SymbolTable st = currentProgram.getSymbolTable();
        long fdVT = 0;
        SymbolIterator sit = st.getSymbolIterator("*CCryPakFindData*", true);
        while (sit.hasNext()) {
            Symbol s = sit.next();
            String nm = s.getName(true);
            println("  symbol: " + nm + " @ 0x" + Long.toHexString(s.getAddress().getOffset()));
            if (nm.contains("vftable") && fdVT == 0) fdVT = s.getAddress().getOffset();
        }
        if (fdVT != 0) {
            println("  CCryPakFindData::vftable @ 0x" + Long.toHexString(fdVT));
            // dump up to 12 slots; decompile the non-null ones (the iterator methods).
            Set<Long> fdMethods = new LinkedHashSet<>();
            for (int i = 0; i < 12; i++) {
                long m = ptr(fdVT + (long) i * 8);
                println("  fd slot " + i + " (+0x" + Long.toHexString((long) i * 8) + ") = 0x" + Long.toHexString(m));
                Function mf = getFunctionAt(a(m));
                if (mf == null) mf = getFunctionContaining(a(m));
                if (mf != null && m != 0) fdMethods.add(mf.getEntryPoint().getOffset());
            }
            int dm = 0;
            for (Long m : fdMethods) {
                if (dm++ >= 8) { println("\n(>8 fd methods, stopping)"); break; }
                decompFull(m, "CCryPakFindData method #" + dm);
            }
        } else {
            println("  CCryPakFindData::vftable symbol NOT found — will need a manual locate");
        }

        di.dispose();
        println("\n===== dump_slot101 COMPLETE =====");
    }
}
