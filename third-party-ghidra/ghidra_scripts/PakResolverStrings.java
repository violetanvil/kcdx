// Resolve the .rdata string literals + cvar provenance behind the pak/loose precedence resolver.
//  - The DAT_* operands FUN_18241ad60 builds its two prefix strings from (mode-3 prefix match).
//  - What writes/reads the pakPriority field at *(*(this+0x228)+0x20) (the ICVar->iValue).
//  - param_1[0x31] (this+0x188) the data-root CryStringT, by sampling a live? no - static only:
//    instead, find xrefs that STORE into +0x228 to name the cvar.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;

public class PakResolverStrings extends GhidraScript {
    AddressSpace sp; Memory mem;
    Address a(long va){ return sp.getAddress(va); }

    String cstr(long va) {
        StringBuilder s = new StringBuilder();
        try {
            for (int i = 0; i < 64; i++) {
                byte b = mem.getByte(a(va + i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) { s.append("\\x").append(String.format("%02x", b & 0xff)); }
                else s.append((char)(b & 0xff));
            }
        } catch (Exception e) { return "<unreadable>"; }
        return s.toString();
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();

        long[] dats = {
            0x183a531c4L, 0x183a3bad0L, 0x183a3bad8L, 0x183a3bac4L, 0x183b26cc0L, // first prefix build
            0x183b777f4L, 0x183a3bae8L, 0x183a3bad4L, 0x183a3bac8L, 0x183a3baccL  // second prefix build
        };
        println("=== FUN_18241ad60 prefix-string literals (CryStringT sources) ===");
        for (long d : dats) {
            println("  0x" + Long.toHexString(d) + " = \"" + cstr(d) + "\"");
        }

        // The two mode strings AdjustFileName / FOpen use:
        println("\n=== misc literals ===");
        long[] misc = { 0x183db31d4L };
        for (long d : misc) println("  0x" + Long.toHexString(d) + " = \"" + cstr(d) + "\"");

        println("\ndone.");
    }
}
