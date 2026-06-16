// slot-101 recon, pass 3 — pin the REAL CCryPakFindData::vftable and its scan method.
//
// pass 2's LEA heuristic grabbed 0x183a646b8 = a _reference_target<int>/I_ModuleMessageListener
// composite (the find-handle-list node sub-object), NOT the returned find-data object's
// vftable. The factory body stores `*puVar1 = CCryPakFindData::vftable` into the RETURNED
// object first. This pass:
//  (1) Full annotated disasm of factory 0x973294 — pin which LEA feeds the FIRST `mov [reg],rax`
//      store into the returned object (rax/rcx from the 0x20 alloc), recovering the true vftable.
//  (2) Resolve CCryPakFindData::vftable by symbol-name lookup (exact, getSymbols).
//  (3) Locate the find-data SCAN method: scan all functions whose body references the true
//      vftable VA AND call _findfirst64 (the FindFirst-on-the-iterator scanner). Decompile it
//      + report: AdjustFileName(slot1) shape? _findfirst64/_findnext64 (DISK)? pak-dir walk
//      (FUN_1804631f0 / loaded-pak vector at +0x120) — disk-only vs unified.
//  (4) Compare: slot-14 ForEachFile (FUN_18241d2e8) body for the same markers.
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
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import java.util.List;

public class dump_realvt extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    String dc(long va) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        if (f == null) return "  (no fn @0x" + Long.toHexString(va) + ")";
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) return r.getDecompiledFunction().getC();
        return "  (decompile failed)";
    }
    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        println("  name: " + f.getName() + "  size: " + f.getBody().getNumAddresses());
        println(dc(va));
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // (1) full disasm of factory.
        println("========== factory 0x973294 — annotated disasm ==========");
        Function fac = getFunctionContaining(a(0x180973294L));
        InstructionIterator it = currentProgram.getListing().getInstructions(fac.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            Reference[] rs = ins.getReferencesFrom();
            StringBuilder b = new StringBuilder();
            for (Reference r : rs) {
                long to = r.getToAddress().getOffset();
                Symbol[] ss = currentProgram.getSymbolTable().getSymbols(r.getToAddress());
                b.append("  ->0x").append(Long.toHexString(to));
                if (ss != null && ss.length > 0) b.append("(").append(ss[0].getName()).append(")");
            }
            println("  0x" + Long.toHexString(ins.getAddress().getOffset()) + ": " + ins.toString() + b);
        }

        // (2) resolve CCryPakFindData::vftable by symbol name (exact).
        println("\n========== symbol lookup: CCryPakFindData* ==========");
        long realVT = 0;
        for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false)) {
            String nm = s.getName(true);
            if (nm.contains("CCryPakFindData")) {
                println("  " + nm + " @ 0x" + Long.toHexString(s.getAddress().getOffset()));
                if (nm.contains("vftable") && realVT == 0) realVT = s.getAddress().getOffset();
            }
        }
        println("  -> CCryPakFindData::vftable = 0x" + Long.toHexString(realVT));

        // (3) dump the real vftable + find the scan method (references vftable OR calls _findfirst64).
        if (realVT != 0) {
            println("\n========== CCryPakFindData::vftable @ 0x" + Long.toHexString(realVT) + " ==========");
            for (int i = 0; i < 8; i++) {
                long m = ptr(realVT + (long) i * 8);
                Function mf = getFunctionContaining(a(m));
                println("  fd slot " + i + " = 0x" + Long.toHexString(m)
                    + (mf != null ? " (" + mf.getName() + ", " + mf.getBody().getNumAddresses() + "b)" : " (no fn)"));
                if (mf != null && m > 0x180000000L && m < 0x183000000L) decompFull(mf.getEntryPoint().getOffset(), "fd method slot " + i);
            }
        }

        // (4) the slot-14 ForEachFile body, for comparison (the KCDX-in-takeover enumerator).
        decompFull(0x18241d2e8L, "slot-14 ForEachFile (FUN_18241d2e8)");

        di.dispose();
        println("\n===== dump_realvt COMPLETE =====");
    }
}
