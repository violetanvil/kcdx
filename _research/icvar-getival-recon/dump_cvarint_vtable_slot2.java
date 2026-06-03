// FINAL gate read (AP3 + research-disassembly §4.5): read the BODY of slot 2 on the
// concrete CXConsoleVariableInt vtable and confirm it returns the CVar's stored int
// value (i.e. [0x10] IS GetIVal, not GetType/GetFlags).
//
// Route: MSVC RTTI. The type-descriptor `.?AVCXConsoleVariableInt@@` @ 0x184a48128
// is pointed at by that class's RTTICompleteObjectLocator (COL); the COL address sits
// at vtable[-1] (the qword immediately BEFORE the vtable start). So:
//   1. find which .rdata qword == COL_addr where *COL points back to the type descriptor;
//      simpler: scan .rdata for a qword pointing to the type-descriptor (that's inside a COL);
//      the COL's own address then appears as a vtable[-1] slot. We instead scan .rdata for
//      a pointer P such that the qword AFTER P is a .text function (vtable start) — i.e. find
//      addresses A where mem[A] = COL and mem[A+8].. are .text ptrs.
//   2. Pragmatic: scan all .rdata 8-byte-aligned addresses; for each addr V, if mem[V-8]
//      points into .rdata (a COL) and that COL's +0xC field (typeDescriptor disp) resolves
//      to our type descriptor, V is the vtable. Then dump V slot 0/1/2 and decompile slot 2.
// To stay robust without full COL parsing, we ALSO brute-scan for any .rdata location V
// whose slots 0,1,2,3 are all .text and whose vtable[-1] (a COL) references the TD; we print
// candidates and decompile slot 2 of each.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpCVarIntGetIVal extends GhidraScript {

    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    long imageBase;

    boolean inText(long va) {
        if (va == 0) return false;
        try {
            MemoryBlock b = mem.getBlock(sp.getAddress(va));
            return b != null && b.isExecute();
        } catch (Exception e) { return false; }
    }
    boolean inRdata(long va) {
        if (va == 0) return false;
        try {
            MemoryBlock b = mem.getBlock(sp.getAddress(va));
            return b != null && b.isInitialized() && !b.isExecute() && b.getName().contains("rdata");
        } catch (Exception e) { return false; }
    }
    long rd(long va) { try { return mem.getLong(sp.getAddress(va)); } catch (Exception e) { return 0; } }
    int rd32(long va) { try { return mem.getInt(sp.getAddress(va)); } catch (Exception e) { return 0; } }

    String decompAt(long fa, String label) throws Exception {
        Function f = getFunctionAt(sp.getAddress(fa));
        if (f == null) f = getFunctionContaining(sp.getAddress(fa));
        if (f == null) return "\n----- " + label + " @ " + sp.getAddress(fa) + " -----\n  NO FUNCTION";
        DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
        String hdr = "\n----- " + label + " @ " + f.getEntryPoint() + " -----\n  proto: " +
                     f.getPrototypeString(true, false) + "  size=" + f.getBody().getNumAddresses() + "\n";
        if (dr != null && dr.decompileCompleted()) return hdr + dr.getDecompiledFunction().getC();
        return hdr + "  (decompile failed)";
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        imageBase = currentProgram.getImageBase().getOffset();

        // The CXConsoleVariableInt type descriptor (RTTI TypeDescriptor object) START is 0x10 BEFORE
        // the name string (TD layout: +0 vftable ptr, +8 spare, +0x10 mangled-name chars). Name @0x184a48128.
        long tdName = 0x184a48128L;
        long tdStart = tdName - 0x10;
        println("CXConsoleVariableInt TypeDescriptor: name@" + Long.toHexString(tdName) + " tdStart@" + Long.toHexString(tdStart));

        // A COL has typeDescriptor as a 32-bit image-relative disp32 at COL+0xC. So tdDisp = tdStart - imageBase.
        long tdDisp = tdStart - imageBase;
        println("imageBase=" + Long.toHexString(imageBase) + "  tdDisp(expected at COL+0xC)=" + Long.toHexString(tdDisp));

        // Scan .rdata for a COL: a location C where rd32(C+0xC) == tdDisp. Then the vtable V has
        // rd(V-8) == C and rd(V) is a .text function.
        println("\n========== scanning .rdata for CXConsoleVariableInt COL + its vtable ==========");
        int hits = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!(b.isInitialized() && !b.isExecute() && b.getName().contains("rdata"))) continue;
            long start = b.getStart().getOffset(), end = b.getEnd().getOffset();
            for (long c = start; c + 0x10 < end && hits < 6; c += 4) {
                if (rd32(c + 0xC) == (int) tdDisp) {
                    // candidate COL at c. find vtable: a V with rd(V-8)==c and rd(V) in .text
                    println("  COL candidate @ " + Long.toHexString(c) + " (COL+0xC == tdDisp)");
                    for (long v = start + 8; v + 0x20 < end; v += 8) {
                        if (rd(v - 8) == c && inText(rd(v)) && inText(rd(v + 8)) && inText(rd(v + 0x10))) {
                            long s0 = rd(v), s1 = rd(v + 8), s2 = rd(v + 0x10), s3 = rd(v + 0x18);
                            println("    >>> VTABLE @ " + Long.toHexString(v));
                            println("        slot0 (dtor?)    = 0x" + Long.toHexString(s0));
                            println("        slot1 (Release)  = 0x" + Long.toHexString(s1));
                            println("        slot2 (GetIVal?) = 0x" + Long.toHexString(s2) + "  <== read this body");
                            println("        slot3 (GetI64?)  = 0x" + Long.toHexString(s3));
                            println(decompAt(s2, "CXConsoleVariableInt vtable[2] BODY"));
                            println(decompAt(s3, "CXConsoleVariableInt vtable[3] BODY (sibling, for contrast)"));
                            hits++;
                            break;
                        }
                    }
                }
            }
        }
        if (hits == 0) println("  (no COL/vtable matched — RTTI layout differs; fall back to ctor xref FUN_180b991ac)");

        println("\ndone.");
    }
}
