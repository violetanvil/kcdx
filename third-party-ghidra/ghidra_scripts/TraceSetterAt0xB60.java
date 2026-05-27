// Trace where +0xB60 is allocated/assigned to find the sub-object type and vtable.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.*;

public class TraceSetterAt0xB60 extends GhidraScript {
    static final long BASE = 0x180000000L;

    String hex(byte[] b, int n) {
        StringBuilder sb = new StringBuilder();
        int m = Math.min(n, b.length);
        for (int i = 0; i < m; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Decompile FUN_18245e7c0 -- the function with the mov [rdi+0xB60], rsi pattern
        Function f = getFunctionAt(sp.getAddress(0x18245e7c0L));
        if (f == null) f = getFunctionContaining(sp.getAddress(0x18245eda6L));
        if (f != null) {
            println("=== Decompile " + f.getName() + " ===");
            DecompileResults dres = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
            if (dres != null && dres.decompileCompleted()) {
                String code = dres.getDecompiledFunction().getC();
                int i = 0;
                for (String line : code.split("\n")) {
                    println(String.format("  %3d: %s", i++, line));
                }
            }
        }

        // 2. The three "add rcx, 0xB60" call sites - what functions are they in?
        long[] callerRvas = { 0x1805605c7L, 0x18056174cL, 0x18056604fL };
        for (long rva : callerRvas) {
            Address a = sp.getAddress(rva);
            Function caller = getFunctionContaining(a);
            println("\n=== Caller " + a + " in " + (caller != null ? caller.getName() + "@" + caller.getEntryPoint() : "<none>") + " ===");
            // Decompile each
            if (caller != null) {
                DecompileResults dres = di.decompileFunction(caller, 60, new ConsoleTaskMonitor());
                if (dres != null && dres.decompileCompleted()) {
                    String code = dres.getDecompiledFunction().getC();
                    int i = 0;
                    for (String line : code.split("\n")) {
                        println(String.format("  %3d: %s", i++, line));
                    }
                }
            }
        }

        // 3. Also look at FUN_18245e7c0 disassembly in detail around 0xB60 setter
        Instruction ins = listing.getInstructionAt(sp.getAddress(0x18245ed60L));
        println("\n--- Disasm around 0x18245eda6 ---");
        for (int k = 0; k < 50 && ins != null; k++) {
            byte[] ib = ins.getBytes();
            println(String.format("  %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
            ins = listing.getInstructionAfter(ins.getAddress());
        }

        // 4. Find ALL xrefs (data refs) to all 5 sites
        long[] sites = { 0x1805605c7L, 0x18056174cL, 0x18056604fL, 0x18245ed94L, 0x18245eda6L };
        for (long s : sites) {
            Reference[] refs = getReferencesTo(sp.getAddress(s));
            println("xrefs to " + Long.toHexString(s) + ": " + refs.length);
        }

        println("\ndone.");
    }
}
