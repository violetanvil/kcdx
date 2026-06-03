// Phase 8.5 mechanism-2: confirm the ABI of the search-path REGISTRAR (CCryPak
// vtable slot 19 = FUN_1819af1a8, RVA 0x19AF1A8) and characterise its callers.
//
// Slot 19 push_backs a CryStringT onto the [this+0x198] search-path vector after a
// dedup _stricmp scan -- canonical CCryPak::AddMod. The body forwards rdx straight
// into FUN_1804628a0(strbuf, rdx) (the CryStringT-from-cstr ctor), so the path arg is
// register-passed and the decompiler lost it. This script:
//  (1) Re-decompiles slot 19 (so the C is captured in THIS dump too).
//  (2) Lists every direct caller of slot 19's FUNCTION BODY (process-wide detour
//      sites) AND decompiles the first few, to read what they pass as the path (rdx)
//      -- the ground-truth ABI confirmation (what a real caller registers).
//  (3) Decompiles the vector push_back helper FUN_18041d3a8 (CryStringT* push_back)
//      and the CryStringT ctor FUN_1806962e0 to nail the element type.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class PakAddModABI extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount() + "  RVA 0x"
            + Long.toHexString(f.getEntryPoint().getOffset() - 0x180000000L));
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long addMod = 0x1819AF1A8L;
        decompFull(addMod, "CCryPak slot19 AddMod (search-path push_back)");

        // (2) callers of the AddMod body -- ground-truth what gets registered.
        println("\n==================================================================");
        println("DIRECT CALLERS of slot19 body 0x" + Long.toHexString(addMod));
        println("==================================================================");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator refs = rm.getReferencesTo(a(addMod));
        int n = 0;
        java.util.LinkedHashSet<Long> callerFns = new java.util.LinkedHashSet<>();
        while (refs.hasNext()) {
            Reference rf = refs.next();
            if (!rf.getReferenceType().isCall() && !rf.getReferenceType().isFlow()) {
                // still record; vtable data ref shows the slot membership
            }
            Address from = rf.getFromAddress();
            Function cf = getFunctionContaining(from);
            String cn = cf != null ? cf.getName() : "(none)";
            long cva = cf != null ? cf.getEntryPoint().getOffset() : 0;
            println("  xref from 0x" + Long.toHexString(from.getOffset())
                + "  type=" + rf.getReferenceType().getName()
                + "  in fn=" + cn + (cf!=null? " (RVA 0x"+Long.toHexString(cva-0x180000000L)+")":""));
            if (cf != null && rf.getReferenceType().isCall()) callerFns.add(cva);
            n++;
        }
        println("  (total xrefs: " + n + "; distinct calling functions: " + callerFns.size() + ")");

        // decompile up to 4 distinct callers to read the registered path (rdx)
        int dumped = 0;
        for (long cva : callerFns) {
            if (dumped++ >= 4) break;
            decompFull(cva, "AddMod caller #" + dumped);
        }

        // (3) the element-type helpers.
        decompFull(0x18041d3a8L, "FUN_18041d3a8 (vector push_back onto [+0x198])");
        decompFull(0x1806962e0L, "FUN_1806962e0 (CryStringT ctor/copy for the new entry)");

        println("\ndone.");
    }
}
