// Phase 8.5a confirm: vtable +0x120 (slot 36) = ICryPak::FOpen open-by-path.
// Evidence chain: WriteCachePak @0x182440d80 calls (*(pCryPak+0x120))(pCryPak, path, "wb"-ish, 0x10004)
// and checks the return for null -> file handle. pCryPak global = DAT_18492b850 = gEnv(0x18492b800)+0x50.
//
// This script:
//  (1) Decompile slot36 fn 0x1804614A0 (claimed FOpen) -> signature + body.
//  (2) Decompile neighbors used by WriteCachePak: +0x148 (slot41 FWrite),
//      +0x1B8 (slot55 FClose) -> corroborate the method family.
//  (3) Resolve the mode string at 0x183db31d4.
//  (4) Read pCryPak global DAT_18492b850 vs gEnv 0x18492b800 (offset check).
//  (5) Dump a few callers of slot36 fn to see real asset-path call sites.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

public class PakFOpenConfirm extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void dump(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n=== " + label + " @ 0x" + Long.toHexString(va) + " ===");
        if (f == null) { println("  (no function)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName() + "  params: " + f.getParameterCount());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 30, monitor);
        if (r != null && r.decompileCompleted()) {
            String c = r.getDecompiledFunction().getC();
            if (c.length() > 1800) c = c.substring(0,1800) + "\n...[trunc]";
            println(c);
        } else println("  (decompile failed)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // (4) pCryPak global vs gEnv offset
        long gEnv = 0x18492B800L;
        long pCryPakSlot = 0x18492B850L;
        println("gEnv VA          = 0x" + Long.toHexString(gEnv));
        println("pCryPak slot VA  = 0x" + Long.toHexString(pCryPakSlot) + "  (= gEnv + 0x" + Long.toHexString(pCryPakSlot - gEnv) + ")");

        // (3) mode string
        Address ms = a(0x183db31d4L);
        StringBuilder modeStr = new StringBuilder();
        for (int i=0;i<8;i++){ byte b = mem.getByte(ms.add(i)); if (b==0) break; modeStr.append((char)(b&0xFF)); }
        println("mode string @0x183db31d4 = \"" + modeStr + "\"");

        // (1) FOpen slot36
        dump(0x1804614A0L, "slot36 (+0x120) CLAIM: ICryPak::FOpen");
        // (2) FWrite slot41, FClose slot55
        dump(0x180A700C8L, "slot41 (+0x148) CLAIM: FWrite");
        dump(0x1804609D0L, "slot55 (+0x1B8) CLAIM: FClose");

        // (5) callers of slot36 fn
        println("\n=== callers of slot36 fn 0x1804614A0 (real FOpen call sites) ===");
        Reference[] refs = getReferencesTo(a(0x1804614A0L));
        int shown=0;
        for (Reference rf : refs) {
            Function cf = getFunctionContaining(rf.getFromAddress());
            println("  ref from 0x" + Long.toHexString(rf.getFromAddress().getOffset()) + " type=" + rf.getReferenceType()
                + " in " + (cf!=null?cf.getName():"?"));
            shown++;
            if (shown>=20) break;
        }
        println("\ndone.");
    }
}
