// Resolve vtable slot[1] called from FUN_180561700 at +0x56
// Run via: analyzeHeadless <proj_dir> KCD2 -process WHGame.dll -postScript FindIsInCombatSlot.java -noanalysis -readOnly
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
import ghidra.program.model.symbol.Symbol;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class FindIsInCombatSlot extends GhidraScript {

    static final long TARGET_FUNC_RVA = 0x561700L;
    static final long CALL_SITE_RVA   = 0x561756L;
    static final long BASE            = 0x180000000L;

    Address addr(long rva) {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        return sp.getAddress(BASE + rva);
    }

    String hexBytes(byte[] b, int count) {
        StringBuilder sb = new StringBuilder();
        int n = Math.min(count, b.length);
        for (int i = 0; i < n; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        println("================================================================================");
        println("Resolving slot[1] of vtable called at 0x180561756");
        println("================================================================================");

        Address funcAddr = addr(TARGET_FUNC_RVA);
        Function func = getFunctionAt(funcAddr);
        if (func == null) func = getFunctionContaining(funcAddr);
        if (func == null) {
            println("No function found at/containing " + funcAddr + " -- creating one");
            func = createFunction(funcAddr, "FUN_180561700");
        }
        println(String.format("\nFunction: %s @ %s  size=%d",
                func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses()));

        // 1. Decompile
        println("\n--- Decompiler output ---");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        DecompileResults res = di.decompileFunction(func, 60, new ConsoleTaskMonitor());
        if (res != null && res.decompileCompleted()) {
            String code = res.getDecompiledFunction().getC();
            int i = 0;
            for (String line : code.split("\n")) {
                println(String.format("  %3d: %s", i++, line));
            }
        } else {
            println("  Decompilation failed: " + (res != null ? res.getErrorMessage() : "no result"));
        }

        // 2. Call site
        println("\n--- Call site instruction inspection ---");
        Listing listing = currentProgram.getListing();
        Address callAddr = addr(CALL_SITE_RVA);
        Instruction callIns = listing.getInstructionAt(callAddr);
        println("  Instruction @" + callAddr + " : " + callIns);
        println("  Bytes: " + hexBytes(callIns.getBytes(), 16));
        Reference[] refsFrom = callIns.getReferencesFrom();
        println("  References from this call site:");
        for (Reference r : refsFrom) {
            Address to = r.getToAddress();
            println("    -> " + to + "  type=" + r.getReferenceType() + " primary=" + r.isPrimary());
            Symbol sym = currentProgram.getSymbolTable().getPrimarySymbol(to);
            if (sym != null) println("       symbol: " + sym.getName());
        }

        // 3. Search for constructor pattern: lea rax, [rip+disp32] ; ... mov [reg+0xB60], rax
        println("\n--- Searching for constructor pattern (lea + mov [obj+0xB60]) ---");
        Memory mem = currentProgram.getMemory();
        int[] modrmVariants = {0x83, 0x86, 0x87, 0x81, 0x82, 0x84, 0x85}; // RBX, RSI, RDI, RCX, RDX, RSP-not-real, RBP
        List<Address> candidates = new ArrayList<>();
        for (int variant : modrmVariants) {
            byte[] needle = new byte[] { 0x48, (byte)0x89, (byte)variant, 0x60, 0x0B, 0x00, 0x00 };
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized() || !blk.isExecute()) continue;
                Address a = mem.findBytes(blk.getStart(), blk.getEnd(), needle, null, true, monitor);
                while (a != null) {
                    candidates.add(a);
                    Address next = a.add(1);
                    if (next.compareTo(blk.getEnd()) > 0) break;
                    a = mem.findBytes(next, blk.getEnd(), needle, null, true, monitor);
                }
            }
        }
        println("  Found " + candidates.size() + " candidate `mov [reg+0xB60], rax` sites");

        Map<Long, List<Address>> vtableCands = new TreeMap<>();
        for (Address c : candidates) {
            // Look back 0..16 bytes for `48 8D 05`
            for (int back = 0; back <= 16; back++) {
                Address probe;
                try { probe = c.subtract(back); } catch (Exception e) { break; }
                int b0, b1, b2;
                try {
                    b0 = mem.getByte(probe) & 0xFF;
                    b1 = mem.getByte(probe.add(1)) & 0xFF;
                    b2 = mem.getByte(probe.add(2)) & 0xFF;
                } catch (Exception e) { continue; }
                if (b0 == 0x48 && b1 == 0x8D && b2 == 0x05) {
                    int d0 = mem.getByte(probe.add(3)) & 0xFF;
                    int d1 = mem.getByte(probe.add(4)) & 0xFF;
                    int d2 = mem.getByte(probe.add(5)) & 0xFF;
                    int d3 = mem.getByte(probe.add(6)) & 0xFF;
                    int disp = d0 | (d1 << 8) | (d2 << 16) | (d3 << 24);
                    long ripAfter = probe.add(7).getOffset();
                    long vtableVa = ripAfter + disp; // disp is signed via Java int
                    vtableCands.computeIfAbsent(vtableVa, k -> new ArrayList<>()).add(probe);
                    break;
                }
            }
        }
        println("  Distinct vtable VAs found: " + vtableCands.size());
        for (Map.Entry<Long, List<Address>> e : vtableCands.entrySet()) {
            println(String.format("    vtable @ 0x%X  (%d constructor sites)", e.getKey(), e.getValue().size()));
        }

        // 4. Dump slots
        println("\n--- Vtable contents ---");
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (Map.Entry<Long, List<Address>> entry : vtableCands.entrySet()) {
            long vtVa = entry.getKey();
            Address vtAddr = sp.getAddress(vtVa);
            long s0, s1, s2, s3;
            try {
                s0 = mem.getLong(vtAddr);
                s1 = mem.getLong(vtAddr.add(8));
                s2 = mem.getLong(vtAddr.add(16));
                s3 = mem.getLong(vtAddr.add(24));
            } catch (Exception ex) {
                println(String.format("  vtable @ 0x%X UNREADABLE: %s", vtVa, ex.getMessage()));
                continue;
            }
            println(String.format("  vtable @ 0x%X:", vtVa));
            println(String.format("    slot[0] = 0x%X", s0));
            println(String.format("    slot[1] = 0x%X  <-- TARGET", s1));
            println(String.format("    slot[2] = 0x%X", s2));
            println(String.format("    slot[3] = 0x%X", s3));
            for (Address s : entry.getValue().subList(0, Math.min(2, entry.getValue().size()))) {
                Function f = getFunctionContaining(s);
                if (f != null) println("    constructor func: " + f.getName() + " @ " + f.getEntryPoint());
                else println("    constructor location: " + s + " (no enclosing func)");
            }
        }

        // 5. Slot[1] analysis
        println("\n--- Slot[1] function analysis ---");
        for (Map.Entry<Long, List<Address>> entry : vtableCands.entrySet()) {
            long vtVa = entry.getKey();
            Address vtAddr = sp.getAddress(vtVa);
            long slot1;
            try { slot1 = mem.getLong(vtAddr.add(8)); } catch (Exception ex) { continue; }
            if (slot1 < BASE || slot1 > BASE + 0x10000000L) continue;
            Address slot1Addr = sp.getAddress(slot1);
            Function fn = getFunctionAt(slot1Addr);
            if (fn == null) fn = getFunctionContaining(slot1Addr);
            println(String.format("\n  Slot[1] target: 0x%X (from vtable 0x%X)", slot1, vtVa));
            if (fn != null) {
                println("    name: " + fn.getName() + "  size=" + fn.getBody().getNumAddresses());
                println("    entry: " + fn.getEntryPoint());
            }
            // 32 bytes
            byte[] buf = new byte[32];
            try {
                mem.getBytes(slot1Addr, buf);
                println("    first 32 bytes: " + hexBytes(buf, 32));
            } catch (Exception ex) {
                println("    cannot read bytes: " + ex.getMessage());
            }
            // First instructions
            if (fn != null) {
                Instruction ins = listing.getInstructionAt(fn.getEntryPoint());
                for (int k = 0; k < 16 && ins != null; k++) {
                    byte[] ib = ins.getBytes();
                    println(String.format("      %s  %-25s  %s", ins.getAddress(), hexBytes(ib, ib.length), ins));
                    boolean isRet = (ib[0] & 0xFF) == 0xC3 || (ib[0] & 0xFF) == 0xC2;
                    ins = listing.getInstructionAfter(ins.getAddress());
                    if (isRet) break;
                }
                // Callers
                Reference[] callerRefs = getReferencesTo(slot1Addr);
                println("    references to this function: " + callerRefs.length);
                int shown = 0;
                for (Reference r : callerRefs) {
                    if (shown++ >= 8) break;
                    Address fromA = r.getFromAddress();
                    Function caller = getFunctionContaining(fromA);
                    println("      <- " + fromA + "  in " + (caller != null ? caller.getName() : "<no func>"));
                }
            }

            // Decompile slot[1] if function exists
            if (fn != null) {
                DecompileResults dres = di.decompileFunction(fn, 30, new ConsoleTaskMonitor());
                if (dres != null && dres.decompileCompleted()) {
                    println("    --- slot[1] decompiled ---");
                    String code = dres.getDecompiledFunction().getC();
                    int j = 0;
                    for (String line : code.split("\n")) {
                        println(String.format("      %3d: %s", j++, line));
                    }
                }
            }
        }

        println("\ndone.");
    }
}
