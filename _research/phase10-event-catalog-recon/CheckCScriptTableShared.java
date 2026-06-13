// CheckCScriptTableShared.java -- how many sites install CScriptTable::vftable?
// (1 ctor install => a single shared concrete script-table class => slot 22
// dispatch is a single global chokepoint for ALL entities). READ refs; report.
//@category Research

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class CheckCScriptTableShared extends GhidraScript {
    private final long IB = 0x180000000L;
    @Override
    public void run() throws Exception {
        Address vtbl = null;
        SymbolIterator syms = currentProgram.getSymbolTable().getAllSymbols(true);
        for (Symbol s : syms) {
            String n = s.getName(true);
            if (n != null && n.contains("CScriptTable") && n.contains("vftable")) { vtbl = s.getAddress(); break; }
        }
        if (vtbl == null) { println("vftable not found"); return; }
        println("CScriptTable::vftable @ " + vtbl + " (RVA 0x" + Long.toHexString(vtbl.getOffset() - IB) + ")");
        int n = 0;
        println("=== references TO the vftable (install/use sites) ===");
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(vtbl)) {
            Function f = getFunctionContaining(r.getFromAddress());
            println(String.format("  from %s  type=%s  fn=%s", r.getFromAddress(),
                    r.getReferenceType(), f != null ? f.getName() + "@" + f.getEntryPoint() : "(none)"));
            n++;
        }
        println("total refs = " + n);

        // Also: dump slot22 target's first xref count proxy — how many call sites
        // reach the dispatcher FUN_180b9ceb4 (indirect via vtable, so direct
        // xrefs are few; the point is the vtable is the shared install).
        Address disp = toAddr(0x180b9ceb4L);
        int dn = 0;
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(disp)) dn++;
        println("direct refs to dispatcher 0xb9ceb4 = " + dn + " (indirect via vtable slot 22 expected)");
        println("=== done ===");
    }
}
