// FRONT 2 (Phase 8.5 pak resolver) -- the pak OPEN / MOUNT / ARCHIVE machinery.
// Builds on the prior U.5 dump (_u5_worker2.txt / _u5b_worker.txt) which decompiled
// the OpenPack wrapper FUN_180da4e5c, the register worker FUN_1804d4824, and the
// open+register FUN_1804d495c. This script closes the open gaps:
//   (A) VERIFY the OpenPack/OpenPacks vtable slot(s) against the live CCryPak vtable
//       @ VA 0x183A95FA8 (AP3 -- never trust a "slot 6" label; read the table).
//   (B) Decompile the per-part open+register leaf FUN_1804d526c (the function that
//       actually mounts ONE pak archive object and inserts it into the pak list).
//   (C) Decompile OpenCachePak FUN_18243fc40 + walk to the ZipDir CDR directory-index
//       BUILD (the Central Directory parse -- the build, not the search).
//   (D) Find the loaded-pak ARRAY the resolver's FUN_1804631f0 binary-searches
//       (member offsets [this+0xf0]/[+0x120]) and the insert into it -- the mount-order
//       data structure. Decompile FUN_1804631f0 to read which member it iterates,
//       and FUN_18087c9e8 (called on param_1+0x308) for the post-mount step.
//   (E) Resolve string anchors "OpenPack"/"OpenPacks" to their LEA xref functions so
//       the plural glob entry the mod-absorb code calls (OpenPacks per
//       enabled_list_builder.cpp:57) is located by name, not guessed.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class PakOpenMount extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    static final long BASE = 0x180000000L;
    Address a(long va){ return sp.getAddress(va); }

    long rva(long va){ return va - BASE; }

    String fnInfo(Function f) {
        if (f == null) return "(no function)";
        return f.getName() + "  conv=" + f.getCallingConventionName()
            + "  params=" + f.getParameterCount()
            + "  RVA 0x" + Long.toHexString(rva(f.getEntryPoint().getOffset()))
            + "\n  sig: " + f.getSignature().getPrototypeString();
    }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va)
            + " (RVA 0x" + Long.toHexString(rva(va)) + ") =====");
        if (f == null) { println("  (no function at this VA)"); return; }
        println("  " + fnInfo(f));
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }

    // (A) Walk the CCryPak vtable and print which slot holds a given function VA.
    void findSlot(long vtableVa, long targetFnVa, int slots, String label) throws MemoryAccessException {
        println("\n----- SLOT SEARCH: " + label + " fn 0x" + Long.toHexString(targetFnVa)
            + " in vtable 0x" + Long.toHexString(vtableVa) + " -----");
        boolean found = false;
        for (int i = 0; i < slots; i++) {
            long slotVa = mem.getLong(a(vtableVa + (long) i * 8));
            if (slotVa == targetFnVa) {
                println("  FOUND: slot " + i + " (vtable+0x" + Long.toHexString(i * 8L)
                    + ") -> 0x" + Long.toHexString(slotVa));
                found = true;
            }
        }
        if (!found) println("  NOT in first " + slots + " slots (fn may not be a vtable method, or wrong table)");
    }

    // Dump the whole vtable so the OpenPack/OpenPacks slots can be eyeballed against the body.
    void dumpVtable(long vtableVa, int slots) throws MemoryAccessException {
        println("\n----- CCryPak VTABLE DUMP 0x" + Long.toHexString(vtableVa)
            + " (" + slots + " slots) -----");
        for (int i = 0; i < slots; i++) {
            long slotVa = mem.getLong(a(vtableVa + (long) i * 8));
            Function f = getFunctionAt(a(slotVa));
            String nm = (f != null) ? f.getName() : "(no fn)";
            println(String.format("  slot %2d (+0x%03x): 0x%x  %s", i, i * 8, slotVa, nm));
        }
    }

    // read a C-string at a VA (for verifying a found address really holds the needle)
    String cstr(long va, int max) {
        StringBuilder s = new StringBuilder();
        try {
            for (int i = 0; i < max; i++) {
                byte b = mem.getByte(a(va + i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) s.append("\\x").append(String.format("%02x", b & 0xff));
                else s.append((char) (b & 0xff));
            }
        } catch (Exception e) { return "<unreadable>"; }
        return s.toString();
    }

    // (E) scan a VA range for the ASCII bytes of a needle; for each hit print the
    //     string and every code xref to that address (the LEA sites -> owning fn).
    void scanAndXref(long lo, long hi, String needle) {
        println("\n----- SCAN for \"" + needle + "\" in 0x" + Long.toHexString(lo)
            + "..0x" + Long.toHexString(hi) + " -----");
        byte[] pat = needle.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        ReferenceManager rm = currentProgram.getReferenceManager();
        int hits = 0;
        try {
            for (long va = lo; va < hi; va++) {
                boolean match = true;
                for (int i = 0; i < pat.length; i++) {
                    if (mem.getByte(a(va + i)) != pat[i]) { match = false; break; }
                }
                if (!match) continue;
                hits++;
                println("  @0x" + Long.toHexString(va) + " = \"" + cstr(va, 96) + "\"");
                ReferenceIterator ri = rm.getReferencesTo(a(va));
                int x = 0;
                while (ri.hasNext()) {
                    Reference rf = ri.next();
                    Address from = rf.getFromAddress();
                    Function cf = getFunctionContaining(from);
                    println("    xref from 0x" + Long.toHexString(from.getOffset())
                        + " type=" + rf.getReferenceType().getName()
                        + " in fn=" + (cf != null ? cf.getName() + " (RVA 0x"
                            + Long.toHexString(rva(cf.getEntryPoint().getOffset())) + ")" : "(none)"));
                    x++;
                }
                if (x == 0) println("    (no xref recorded to this exact address)");
            }
        } catch (MemoryAccessException e) {
            println("  (scan hit unmapped memory at boundary -- partial)");
        }
        if (hits == 0) println("  (needle not found in range)");
    }

    // xrefs to a KNOWN string address (already located in the U.5 dump)
    void xrefsToStr(long strVa) {
        println("\n----- xrefs to string @0x" + Long.toHexString(strVa)
            + " = \"" + cstr(strVa, 80) + "\" -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator ri = rm.getReferencesTo(a(strVa));
        int x = 0;
        while (ri.hasNext()) {
            Reference rf = ri.next();
            Address from = rf.getFromAddress();
            Function cf = getFunctionContaining(from);
            println("    xref from 0x" + Long.toHexString(from.getOffset())
                + " type=" + rf.getReferenceType().getName()
                + " in fn=" + (cf != null ? cf.getName() + " (RVA 0x"
                    + Long.toHexString(rva(cf.getEntryPoint().getOffset())) + ")" : "(none)"));
            x++;
        }
        if (x == 0) println("    (no xref recorded)");
    }

    // list direct callers of a function body
    void callers(long va, String label) {
        println("\n----- CALLERS of " + label + " 0x" + Long.toHexString(va) + " -----");
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator ri = rm.getReferencesTo(a(va));
        int n = 0;
        java.util.LinkedHashSet<Long> cf = new java.util.LinkedHashSet<>();
        while (ri.hasNext()) {
            Reference rf = ri.next();
            Address from = rf.getFromAddress();
            Function f = getFunctionContaining(from);
            println("  xref from 0x" + Long.toHexString(from.getOffset())
                + " type=" + rf.getReferenceType().getName()
                + " in fn=" + (f != null ? f.getName() + " (RVA 0x"
                    + Long.toHexString(rva(f.getEntryPoint().getOffset())) + ")" : "(none)"));
            if (f != null && rf.getReferenceType().isCall()) cf.add(f.getEntryPoint().getOffset());
            n++;
        }
        println("  (total xrefs " + n + "; distinct callers " + cf.size() + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long VTABLE = 0x183A95FA8L;
        long OPENPACK_WRAP = 0x180da4e5cL;   // U.5: slot-6 OpenPack wrapper (VERIFY)
        long REG_WORKER    = 0x1804d4824L;   // U.5: register worker
        long OPEN_REGISTER = 0x1804d495cL;   // U.5: open+register (split paks; writes +0x318)
        long PER_PART_LEAF = 0x1804d526cL;   // U.5b: the per-part open+register leaf (mount one pak)
        long OPENCACHEPAK  = 0x18243fc40L;   // U.5: OpenCachePak
        long PAK_MEMBERSHIP= 0x1804631f0L;   // resolver's pak-membership binary search (iterates the list)
        long POST_MOUNT    = 0x18087c9e8L;   // called on param_1+0x308 after a successful mount

        // (A) verify the slot of the OpenPack wrapper + dump the table for OpenPacks.
        dumpVtable(VTABLE, 96);
        findSlot(VTABLE, OPENPACK_WRAP, 96, "OpenPack wrapper");

        // (E) name-resolve OpenPack / OpenPacks (the plural glob entry) by scanning .rdata.
        //     .rdata window bracketed by the U.5 string addresses (0x183a93c00..0x1850xxxxx).
        long RDATA_LO = 0x183A00000L, RDATA_HI = 0x185100000L;
        scanAndXref(RDATA_LO, RDATA_HI, "OpenPacks");
        scanAndXref(RDATA_LO, RDATA_HI, "OpenPack ");        // "OpenPack " with format/space
        scanAndXref(RDATA_LO, RDATA_HI, "ICryPak::OpenPack");
        scanAndXref(RDATA_LO, RDATA_HI, "Pak file");         // common open-time format hits

        // (D) the loaded-pak array + mount-order: decompile the membership search
        //     (reads which member offset it walks), the per-part mount leaf, and the
        //     post-mount step.
        decompFull(PAK_MEMBERSHIP, "FUN_1804631f0 pak-membership search (reads loaded-pak array member)");
        decompFull(PER_PART_LEAF, "FUN_1804d526c per-part open+register (mounts ONE pak)");
        decompFull(POST_MOUNT, "FUN_18087c9e8 post-mount step (on this+0x308)");

        // (B/C) re-capture wrapper + worker + OpenCachePak (so this dump is self-contained),
        //       then walk OpenCachePak to the CDR build.
        decompFull(OPENPACK_WRAP, "FUN_180da4e5c OpenPack wrapper");
        decompFull(REG_WORKER, "FUN_1804d4824 OpenPack register worker");
        decompFull(OPEN_REGISTER, "FUN_1804d495c open+register (split-aware)");
        decompFull(OPENCACHEPAK, "FUN_18243fc40 OpenCachePak");

        // (C) the CDR directory-index BUILD anchors -- xref the ZipDir CDR error strings
        //     (addresses captured in the U.5 dump) to their owning functions (the archive
        //     directory parse = the index BUILD).
        xrefsToStr(0x183dd1180L); // "Archive contains corrupted CDR."
        xrefsToStr(0x183dd0ae0L); // "Cannot find Central Directory Record in pak..."
        xrefsToStr(0x183dd0930L); // "The central directory offset or size are out of range..."
        xrefsToStr(0x183dd0a40L); // "The file is too small, it doesn't even contain the CDREnd..."
        xrefsToStr(0x183dd11e0L); // "Central Directory record is either corrupt, or truncated..."
        xrefsToStr(0x183dd1250L); // "Central Directory contains file descriptors pointing outside..."
        xrefsToStr(0x1846ab920L); // "OpenCachePak '%s' ERROR during OpenPack '%s'"

        // callers of the per-part mount leaf + OpenCachePak + the register worker, to
        // confirm the full open call-tree top-down.
        callers(PER_PART_LEAF, "FUN_1804d526c per-part mount");
        callers(REG_WORKER, "FUN_1804d4824 register worker");
        callers(OPENPACK_WRAP, "FUN_180da4e5c OpenPack wrapper");

        println("\nDONE.");
    }
}
