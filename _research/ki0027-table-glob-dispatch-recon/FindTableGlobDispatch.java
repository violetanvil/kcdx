// KI-0027 step 5 (authoritative): find every function that references the Libs\Tables\ (0x183a93c10)
// OR __ (0x183a93c20) string, decompile it, and scan the C text for a CCryPak enumeration dispatch.
// We detect the CCryPak object as it is reached via gEnv (the engine global) and a vtable call at
// +0x70 (slot14 ForEachFile) or +0x328 (slot101 FindFirst) or +0x68 (slot13 IsFolder).
// Also flag any '*' string LEA used in those functions.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import java.util.LinkedHashSet;
import java.util.Set;

public class FindTableGlobDispatch extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    Set<Function> refs(long strVa) {
        Set<Function> out = new LinkedHashSet<>();
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Reference r : rm.getReferencesTo(a(strVa))) {
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) out.add(f);
        }
        return out;
    }

    void scan(Function f) {
        DecompileResults r = di.decompileFunction(f, 120, monitor);
        if (r == null || !r.decompileCompleted()) return;
        String c = r.getDecompiledFunction().getC();
        // CCryPak enumeration slot dispatch tells
        boolean s14 = c.contains("+ 0x70)") || c.contains("0x70))");
        boolean s101 = c.contains("+ 0x328)") || c.contains("0x328))");
        boolean s13 = c.contains("+ 0x68)") || c.contains("0x68))");
        boolean star = c.contains("\"*") || c.contains("DAT_183a93c") /*__/Libs/xml family*/;
        if (s14 || s101 || s13) {
            println("\n>>> " + f.getName() + " @ 0x" + f.getEntryPoint()
                + "   slot14(+0x70)=" + s14 + "  slot101(+0x328)=" + s101 + "  slot13(+0x68)=" + s13);
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        Set<Function> all = new LinkedHashSet<>();
        all.addAll(refs(0x183a93c10L)); // Libs\Tables\
        all.addAll(refs(0x183a93c20L)); // __
        println("functions referencing Libs\\Tables\\ or __ : " + all.size());
        for (Function f : all) println("   " + f.getName() + " @ 0x" + f.getEntryPoint());
        println("\n--- those with a CCryPak enum-slot dispatch in body ---");
        for (Function f : all) scan(f);
        println("\n========== done ==========");
    }
}
