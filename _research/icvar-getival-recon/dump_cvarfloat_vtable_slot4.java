// Verify ICVar::GetFVal slot (the float-read accessor) the same way GetIVal was:
// resolve the concrete CXConsoleVariableFloat vtable via RTTI and read slot-4's body.
// Canonical ICVar.h: ~ICVar(0), Release(1), GetIVal(2), GetI64Val(3), GetFVal(4),
// GetString(5). GetFVal expected at slot 4 / +0x20, returns float.
// RTTI type-descriptor name string for the float concrete class: `.?AVCXConsoleVariableFloat@@`.
// Find its name string, walk COL -> vtable, dump slots 2/3/4/5, decompile slot-4.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpCVarFloatGetFVal extends GhidraScript {

    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    long imageBase;

    boolean inText(long va) {
        if (va == 0) return false;
        try { MemoryBlock b = mem.getBlock(sp.getAddress(va)); return b != null && b.isExecute(); }
        catch (Exception e) { return false; }
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

        // Find the CXConsoleVariableFloat type-descriptor name string.
        long tdName = 0;
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (d == null) continue;
            Object v = d.getValue();
            if (v != null && v.toString().equals(".?AVCXConsoleVariableFloat@@")) {
                tdName = d.getAddress().getOffset();
                println("CXConsoleVariableFloat TypeDescriptor name @ " + Long.toHexString(tdName));
                break;
            }
        }
        if (tdName == 0) { println("  (CXConsoleVariableFloat type descriptor not found)"); return; }

        long tdStart = tdName - 0x10;
        long tdDisp = tdStart - imageBase;
        println("tdStart@" + Long.toHexString(tdStart) + "  tdDisp=" + Long.toHexString(tdDisp));

        int hits = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!(b.isInitialized() && !b.isExecute() && b.getName().contains("rdata"))) continue;
            long start = b.getStart().getOffset(), end = b.getEnd().getOffset();
            for (long c = start; c + 0x10 < end && hits < 4; c += 4) {
                if (rd32(c + 0xC) == (int) tdDisp) {
                    for (long vt = start + 8; vt + 0x30 < end; vt += 8) {
                        if (rd(vt - 8) == c && inText(rd(vt)) && inText(rd(vt + 8)) && inText(rd(vt + 0x10))) {
                            long s2 = rd(vt + 0x10), s3 = rd(vt + 0x18), s4 = rd(vt + 0x20), s5 = rd(vt + 0x28);
                            println("\n  >>> CXConsoleVariableFloat VTABLE @ " + Long.toHexString(vt));
                            println("      slot2 (GetIVal)  = 0x" + Long.toHexString(s2));
                            println("      slot3 (GetI64Val)= 0x" + Long.toHexString(s3));
                            println("      slot4 (GetFVal?) = 0x" + Long.toHexString(s4) + "  <== read this");
                            println("      slot5 (GetString)= 0x" + Long.toHexString(s5));
                            println(decompAt(s4, "CXConsoleVariableFloat vtable[4] BODY (GetFVal?)"));
                            println(decompAt(s2, "CXConsoleVariableFloat vtable[2] BODY (GetIVal, for contrast)"));
                            hits++;
                            break;
                        }
                    }
                }
            }
        }
        if (hits == 0) println("  (no COL/vtable matched for the float class)");
        println("\ndone.");
    }
}
