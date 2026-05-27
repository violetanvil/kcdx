// Dump the two combat-state-wrapper functions found at 0x1805605b8 and 0x180566040.
// These return bool based on combat state enum. They are the most likely candidates
// for "IsInCombat" / "IsInPeace" style getters that can be force-patched.
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
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.*;

public class DumpIsInCombatWrappers extends GhidraScript {
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

        long[] funcs = { 0x1805605b8L, 0x180566040L };
        String[] names = { "FUN_1805605b8 (state==2 wrapper)", "FUN_180566040 (state==1 wrapper)" };
        for (int idx = 0; idx < funcs.length; idx++) {
            long fa = funcs[idx];
            Address a = sp.getAddress(fa);
            Function f = getFunctionAt(a);
            if (f == null) f = getFunctionContaining(a);
            println("\n================================================================================");
            println(names[idx] + " @ " + a);
            println("================================================================================");
            if (f == null) { println("  no function"); continue; }
            println("  Function size: " + f.getBody().getNumAddresses());
            println("  Entry: " + f.getEntryPoint());

            // Dump first 64 bytes
            byte[] buf = new byte[64];
            try {
                mem.getBytes(f.getEntryPoint(), buf);
                println("  first 64 bytes: " + hex(buf, 64));
            } catch (Exception e) { /* */ }

            // Disasm
            println("  --- disassembly ---");
            Instruction ins = listing.getInstructionAt(f.getEntryPoint());
            int n = 0;
            while (ins != null && n < 30 && f.getBody().contains(ins.getAddress())) {
                byte[] ib = ins.getBytes();
                println(String.format("    %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
                ins = listing.getInstructionAfter(ins.getAddress());
                n++;
            }

            // Callers
            Reference[] refs = getReferencesTo(f.getEntryPoint());
            println("  references to this function: " + refs.length);
            int shown = 0;
            for (Reference r : refs) {
                if (shown++ >= 12) break;
                Address fromA = r.getFromAddress();
                Function caller = getFunctionContaining(fromA);
                println("    <- " + fromA + "  in " + (caller != null ? caller.getName() + "@" + caller.getEntryPoint() : "<no func>") + "  type=" + r.getReferenceType());
            }
        }

        // Also dump the vftable that's near 0xB60 site to clarify the embedded class.
        // The decompiled constructor said param_1[0x16b] holds a C_ModelProperty<bool,...,combatmodule::C_CombatActorModelOwnership>::vftable.
        // Look at offsets near 0xB58-0xB60-0xB68 in the constructor to find the actual vftable
        // assignment at 0xB60. Let me search the disassembly of FUN_1810eea6c for displacement bytes
        // matching 0xB58, 0xB60, 0xB68:
        println("\n\n=== Searching FUN_1810eea6c disassembly for explicit +0xB5X/+0xB6X displacements ===");
        Function ctor = getFunctionAt(sp.getAddress(0x1810eea6cL));
        if (ctor != null) {
            Instruction ins = listing.getInstructionAt(ctor.getEntryPoint());
            int count = 0;
            while (ins != null && ctor.getBody().contains(ins.getAddress())) {
                byte[] ib = ins.getBytes();
                String hxs = hex(ib, ib.length);
                // Look for any of these displacement byte sequences (LE):
                // 0xB50 = 50 0B 00 00
                // 0xB58 = 58 0B 00 00
                // 0xB60 = 60 0B 00 00
                // 0xB68 = 68 0B 00 00
                // 0xB70 = 70 0B 00 00
                if (hxs.contains("50 0B 00 00") || hxs.contains("58 0B 00 00") ||
                    hxs.contains("60 0B 00 00") || hxs.contains("68 0B 00 00") ||
                    hxs.contains("70 0B 00 00")) {
                    println(String.format("    %s  %-30s  %s", ins.getAddress(), hxs, ins));
                }
                ins = listing.getInstructionAfter(ins.getAddress());
                count++;
                if (count > 4000) break;
            }
        }

        println("\ndone.");
    }
}
