// Phase 8.5 mechanism-2: find the CCryPak search-path REGISTRAR.
//
// The slot-1 sub-resolver FUN_18046205c iterates a search-path vector at
// [this+0x198] (begin) .. [this+0x1a0] (end), 8-byte CryStringT* entries, looping
// backwards (lVar5=end; lVar5!=begin; lVar5-=8) building "<entry>/<path>" per entry
// (verified: _subresolver_decomp.txt lines 92-99). The alias substitution table is
// at [this+0x1b0]..[this+0x1b8] (FUN_180462664). This script finds the method(s)
// that WRITE those vectors (the registrar / AddMod / SetAlias / OpenPack mount).
//
// It:
//  (1) Dumps the FULL CCryPak vtable (all slots) @ 0x183A95FA8 -> slot/RVA/fn map.
//  (2) For every vtable slot, decompiles it and reports whether its body references
//      +0x198 / +0x1a0 (the search-path vector) or +0x1b0 / +0x1b8 (the alias table)
//      -- and whether the reference is a WRITE (push_back / store) site.
//  (3) Full-decompiles the slots flagged as touching those members (the registrar
//      candidates) + the alias-table walker FUN_180462664 + the CryStringT/vector
//      helpers used by the resolver loop (FUN_1804628a0).
//  (4) Walks the data xrefs to the vtable + a body-scan over the whole .text for any
//      function that stores into [reg+0x198]/[reg+0x1a0] on a CCryPak-shaped this,
//      in case the registrar is a NON-virtual member (not in the vtable).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.ArrayList;
import java.util.List;

public class PakSearchPathAPI extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    String decompC(long va) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        if (f == null) return null;
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) return r.getDecompiledFunction().getC();
        return null;
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount() + "  entry: 0x"
            + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size: " + f.getBody().getNumAddresses());
        String c = decompC(va);
        println(c != null ? c : "  (decompile failed)");
    }

    // does the decompiled C reference these struct-member offsets?
    boolean hits(String c, String[] needles) {
        if (c == null) return false;
        for (String n : needles) if (c.contains(n)) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long vt = 0x183A95FA8L; // CCryPak vtable VA
        println("==================================================================");
        println("CCryPak vtable @ 0x" + Long.toHexString(vt) + "  (full slot dump)");
        println("==================================================================");

        // (1) full vtable dump -- read slots until the pointer is not in an executable
        //     block (the vtable end).
        List<Long> slotVa = new ArrayList<>();
        for (int i = 0; i < 96; i++) {
            long target;
            try { target = ptr(vt + 8L*i); } catch (Exception e) { break; }
            MemoryBlock b = mem.getBlock(a(target));
            boolean exec = (b != null && b.isExecute());
            Function tf = exec ? getFunctionContaining(a(target)) : null;
            if (!exec) {
                println("  slot " + i + " (+0x" + Long.toHexString(8L*i) + ") = 0x"
                    + Long.toHexString(target) + "  <non-exec -> vtable end at slot " + i + ">");
                break;
            }
            slotVa.add(target);
            println("  slot " + i + " (+0x" + Long.toHexString(8L*i) + ") = 0x"
                + Long.toHexString(target) + "  RVA 0x" + Long.toHexString(target - 0x180000000L)
                + (tf != null ? "  fn=" + tf.getName() : "  (no fn)"));
        }

        // (2) per-slot member-reference scan
        String[] spVec = { "0x198", "0x1a0" };   // search-path vector begin/end
        String[] alias = { "0x1b0", "0x1b8" };    // alias substitution table begin/end
        println("\n==================================================================");
        println("SLOT MEMBER-REFERENCE SCAN (which slots touch the search-path vector");
        println("[+0x198/+0x1a0] or the alias table [+0x1b0/+0x1b8])");
        println("==================================================================");
        List<Long> spCandidates = new ArrayList<>();
        List<Long> aliasCandidates = new ArrayList<>();
        for (int i = 0; i < slotVa.size(); i++) {
            long va = slotVa.get(i);
            String c = decompC(va);
            boolean sp198 = hits(c, spVec);
            boolean al1b0 = hits(c, alias);
            if (sp198 || al1b0) {
                StringBuilder tag = new StringBuilder();
                if (sp198) { tag.append(" [SEARCH-PATH +0x198/+0x1a0]"); spCandidates.add(va); }
                if (al1b0) { tag.append(" [ALIAS +0x1b0/+0x1b8]"); aliasCandidates.add(va); }
                println("  slot " + i + " @ 0x" + Long.toHexString(va) + " RVA 0x"
                    + Long.toHexString(va - 0x180000000L) + tag);
            }
        }
        println("  (search-path candidate slots: " + spCandidates.size()
            + "; alias candidate slots: " + aliasCandidates.size() + ")");

        // (3) full-decompile every candidate slot + the known helpers.
        println("\n==================================================================");
        println("FULL DECOMPILE -- search-path-vector candidate slots");
        println("==================================================================");
        for (long va : spCandidates) decompFull(va, "SEARCH-PATH candidate slot");
        println("\n==================================================================");
        println("FULL DECOMPILE -- alias-table candidate slots");
        println("==================================================================");
        for (long va : aliasCandidates) decompFull(va, "ALIAS candidate slot");

        // helpers used by the resolver loop + the alias walker (characterize the vector
        // element type + the alias substitution shape)
        decompFull(0x180462664L, "FUN_180462664 alias-substitution-table walker [this+0x1b0..0x1b8]");
        decompFull(0x1804628a0L, "FUN_1804628a0 (CryStringT-from-entry: builds search string from a vector entry)");

        // (4) non-virtual fallback: scan ALL functions whose body stores into [reg+0x198]
        //     or [reg+0x1a0] (a push_back/store onto the search-path vector) in case the
        //     registrar is not a virtual method.
        println("\n==================================================================");
        println("NON-VIRTUAL SCAN -- functions with an instruction writing [reg+0x198]");
        println("or [reg+0x1a0] (vector store sites). RVA + fn name only; decompile the");
        println("ones whose name/shape looks like a CCryPak registrar.");
        println("==================================================================");
        FunctionManager fm = currentProgram.getFunctionManager();
        int storeHits = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            boolean flagged = false;
            String why = null;
            while (it.hasNext()) {
                Instruction ins = it.next();
                String s = ins.toString();
                // a store (mnemonic MOV with a memory dest) referencing +0x198/+0x1a0,
                // and the dest is a [reg + 0x198] form (not rip-relative data).
                boolean writes198 = (s.contains("0x198") || s.contains("0x1a0"))
                    && s.startsWith("MOV") && s.contains("ptr [")
                    && !s.contains("RIP");
                // accept only when the +0x198/+0x1a0 is the DEST operand (before the comma)
                if (writes198) {
                    int comma = s.indexOf(',');
                    String dest = comma > 0 ? s.substring(0, comma) : s;
                    if (dest.contains("0x198") || dest.contains("0x1a0")) {
                        flagged = true; why = s; break;
                    }
                }
            }
            if (flagged) {
                storeHits++;
                long fva = f.getEntryPoint().getOffset();
                println("  STORE-SITE fn=" + f.getName() + " @ 0x" + Long.toHexString(fva)
                    + " RVA 0x" + Long.toHexString(fva - 0x180000000L) + "   insn: " + why);
            }
        }
        println("  (total store-into-+0x198/+0x1a0 functions: " + storeHits + ")");

        println("\ndone.");
    }
}
