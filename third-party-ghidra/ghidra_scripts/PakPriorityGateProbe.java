// Identify DAT_18556f2a0 — the gate guarding the conditional SetIVal(sys_PakPriority,0)
// in FUN_180da342c. Find every reference (who writes it / its provenance) and any nearby
// string anchor that names the path (editor / shadercache-build / dedi-server etc).
//@category KCD2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class PakPriorityGateProbe extends GhidraScript {
    AddressSpace sp; Memory mem;
    Address a(long va){ return sp.getAddress(va); }
    void refsOf(long va, String what) throws Exception {
        println("\n=== refs to "+what+" @0x"+Long.toHexString(va)+" ===");
        try { println("  current int = 0x"+Integer.toHexString(mem.getInt(a(va)))); } catch(Exception e){ println("  <unreadable/.bss=0>"); }
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a(va));
        int n=0;
        while (ri.hasNext()){
            Reference r=ri.next(); Address f=r.getFromAddress();
            Function fn=getFunctionContaining(f); Instruction ins=getInstructionAt(f);
            println("  ["+(++n)+"] 0x"+Long.toHexString(f.getOffset())+" "+r.getReferenceType()
                +(fn!=null?" fn="+fn.getName()+"@0x"+Long.toHexString(fn.getEntryPoint().getOffset()):"")
                +(ins!=null?"  "+ins.toString():""));
        }
        if(n==0) println("  (none)");
    }
    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace(); mem=currentProgram.getMemory();
        refsOf(0x18556f2a0L,"DAT_18556f2a0 (the Set-to-0 gate)");
        refsOf(0x18556633bcL & 0xFFFFFFFFFFL,"DAT_1856633bc (logallocations seed, sanity)");
        println("\ndone.");
    }
}
