// Decompile/disassemble the constructor FUN_1810eea6c and look for all vtable assignments,
// especially ones near `+0xB60` (which may be `lea rcx, [rbx+0xB60]; mov [rcx], vt` or similar
// reading/writing the inner sub-object's vtable).
// Also: find all callers of FUN_1809dabf0 candidate vs. other slot[1] candidates of vtables
// near 0xB60 to see how the call site routes.
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
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.*;

public class InspectCtor extends GhidraScript {
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
        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Decompile FUN_1810eea6c - the suspected parent constructor
        Function ctor = getFunctionAt(sp.getAddress(0x1810eea6cL));
        if (ctor == null) ctor = getFunctionContaining(sp.getAddress(0x1810ef3d9L));
        if (ctor != null) {
            println("=== Decompile " + ctor.getName() + " ===");
            DecompileResults dres = di.decompileFunction(ctor, 60, new ConsoleTaskMonitor());
            if (dres != null && dres.decompileCompleted()) {
                String code = dres.getDecompiledFunction().getC();
                int i = 0;
                for (String line : code.split("\n")) {
                    println(String.format("  %3d: %s", i++, line));
                }
            }
            // Dump the disassembly around the +0xB60 store and look for other lea+mov patterns
            println("\n--- Disassembly of " + ctor.getName() + " (first 200 instrs) ---");
            Instruction ins = listing.getInstructionAt(ctor.getEntryPoint());
            int count = 0;
            while (ins != null && count < 300) {
                byte[] ib = ins.getBytes();
                println(String.format("  %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
                if (!ctor.getBody().contains(ins.getAddress())) break;
                ins = listing.getInstructionAfter(ins.getAddress());
                count++;
            }
        }

        // 2. Look at callers of the suspect parent function -- maybe it's called WITH +0xB60 offset elsewhere
        // and that caller's `this` is typed.
        println("\n\n=== Other vtable candidates near a 0xB60 reference ===");

        // 3. Look at the FUN_1805616e8 body around 0x180561745 to see the full context for what comes from `(*(this+8) + 0x90)`
        Function srcFn = getFunctionAt(sp.getAddress(0x1805616e8L));
        if (srcFn != null) {
            println("\n--- Disassembly of " + srcFn.getName() + " (around +0xB60 site) ---");
            Instruction ins = listing.getInstructionAt(sp.getAddress(0x180561720L));
            int count = 0;
            while (ins != null && count < 40) {
                byte[] ib = ins.getBytes();
                println(String.format("  %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
                ins = listing.getInstructionAfter(ins.getAddress());
                count++;
            }
        }

        // 4. Look for other call sites with the same vtable+0x8 call pattern
        // Pattern: mov rax, [rcx] ; call [rax+8] = `48 8B 01 FF 50 08`
        // BUT preceded recently by `48 81 C? 60 0B 00 00` (add rcx, 0xB60)
        // Or by `48 8D 8? 60 0B 00 00` (lea rcx, [..+0xB60])
        println("\n\n=== All call sites: mov rax,[rcx]; call [rax+8] preceded by +0xB60 setup ===");
        byte[] needle = new byte[] { 0x48, (byte)0x8B, 0x01, (byte)0xFF, 0x50, 0x08 };
        int matches = 0;
        List<Address> callSites = new ArrayList<>();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized() || !blk.isExecute()) continue;
            Address a = mem.findBytes(blk.getStart(), blk.getEnd(), needle, null, true, monitor);
            while (a != null) {
                // Look back 32 bytes for `+0x0B 0x60 0x00 0x00`-ending pattern (=disp32 of 0xB60 in LE)
                // Actually: 0x60 0x0B 0x00 0x00 (LE) for disp32 == 0xB60.
                boolean precededBy0xB60 = false;
                for (int back = 1; back <= 32; back++) {
                    try {
                        int b0 = mem.getByte(a.subtract(back)) & 0xFF;
                        int b1 = mem.getByte(a.subtract(back).add(1)) & 0xFF;
                        int b2 = mem.getByte(a.subtract(back).add(2)) & 0xFF;
                        int b3 = mem.getByte(a.subtract(back).add(3)) & 0xFF;
                        if (b0 == 0x60 && b1 == 0x0B && b2 == 0x00 && b3 == 0x00) {
                            precededBy0xB60 = true;
                            break;
                        }
                    } catch (Exception e) { break; }
                }
                if (precededBy0xB60) {
                    callSites.add(a);
                    matches++;
                }
                Address next = a.add(1);
                if (next.compareTo(blk.getEnd()) > 0) break;
                a = mem.findBytes(next, blk.getEnd(), needle, null, true, monitor);
            }
        }
        println("Found " + matches + " call sites near +0xB60");
        for (Address cs : callSites) {
            Function cf = getFunctionContaining(cs);
            println("  " + cs + "  in " + (cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "<no func>"));
            // print 8 instrs before and 3 after for context
            // collect 8 prior
            List<Instruction> before = new ArrayList<>();
            Instruction cur = listing.getInstructionBefore(cs);
            for (int k = 0; k < 8 && cur != null; k++) {
                before.add(0, cur);
                cur = listing.getInstructionBefore(cur.getAddress());
            }
            for (Instruction in : before) {
                byte[] ib = in.getBytes();
                println(String.format("      %s  %-30s  %s", in.getAddress(), hex(ib, ib.length), in));
            }
            Instruction ins = listing.getInstructionAt(cs);
            for (int k = 0; k < 4 && ins != null; k++) {
                byte[] ib = ins.getBytes();
                println(String.format("    > %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
                ins = listing.getInstructionAfter(ins.getAddress());
            }
            println();
        }

        // 5. Also try a wider call pattern -- some compilers emit:
        // mov rcx, [rax+0x90]
        // mov rax, [rcx + 0xB60]   <-- single MOV-load form rather than ADD+MOV
        // call [rax+8]
        //
        // Encoding: `48 8B 81 60 0B 00 00 FF 50 08` (mov rax, [rcx+0xB60]; call [rax+8])
        // Or `48 8B 8x 60 0B 00 00 FF 50 08` where x indexes the base register.
        println("\n\n=== Alt pattern: `mov rax, [reg+0xB60]; call [rax+8]` ===");
        // Search for `48 8B ?? 60 0B 00 00 FF 50 08` -- middle byte varies
        for (int modrm = 0x80; modrm <= 0x87; modrm++) {
            byte[] needle2 = new byte[] { 0x48, (byte)0x8B, (byte)modrm, 0x60, 0x0B, 0x00, 0x00, (byte)0xFF, 0x50, 0x08 };
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized() || !blk.isExecute()) continue;
                Address a = mem.findBytes(blk.getStart(), blk.getEnd(), needle2, null, true, monitor);
                while (a != null) {
                    Function f = getFunctionContaining(a);
                    println(String.format("  %s  (modrm=0x%02X) in %s", a, modrm,
                            f != null ? f.getName() + "@" + f.getEntryPoint() : "<no func>"));
                    Address next = a.add(1);
                    if (next.compareTo(blk.getEnd()) > 0) break;
                    a = mem.findBytes(next, blk.getEnd(), needle2, null, true, monitor);
                }
            }
        }

        println("\ndone.");
    }
}
