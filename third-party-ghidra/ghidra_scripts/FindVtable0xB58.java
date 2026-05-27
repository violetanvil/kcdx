// The constructor stores RAX into BOTH +0xB58 AND +0xB60 -- meaning the SAME vtable
// pointer goes to both offsets. Find the lea that loaded RAX before the first store.
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
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.util.task.ConsoleTaskMonitor;

public class FindVtable0xB58 extends GhidraScript {
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

        // Dump disassembly from 0x1810ef380 backwards looking for the lea
        println("--- Disasm around 0x1810ef3c9 (search for lea preceding the +0xB58 store) ---");
        Instruction ins = listing.getInstructionAt(sp.getAddress(0x1810ef380L));
        int n = 0;
        while (ins != null && n < 30) {
            byte[] ib = ins.getBytes();
            println(String.format("  %s  %-30s  %s", ins.getAddress(), hex(ib, ib.length), ins));
            ins = listing.getInstructionAfter(ins.getAddress());
            n++;
        }

        // Once we know the vtable, dump its slot[0..7] and resolve slot[1].
        // Likely the lea is something like `48 8D 05 ?? ?? ?? ??` for `lea rax, [rip+disp]`.
        // It would precede 1810ef3c9 by some bytes.

        println("\ndone.");
    }
}
