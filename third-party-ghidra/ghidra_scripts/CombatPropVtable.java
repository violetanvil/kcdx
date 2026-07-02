// Decompile the bool-combat C_ModelProperty vtable (VA 0x183b091e0) slots and find setter callers.
// vtable located via RTTI COL search (CombatPropSetter recon + python): slot1=getter 0x181a7dac0.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class CombatPropVtable extends GhidraScript {
    static final long VT = 0x183b091e0L;
    long textLo=0, textHi=0;
    Memory mem; AddressSpace sp; DecompInterface di;
    boolean inText(long v){ return v>=textLo && v<textHi; }
    void decomp(long rva, String tag) throws Exception {
        Address a = sp.getAddress(rva);
        Function f = getFunctionAt(a); if (f==null) f=getFunctionContaining(a);
        if (f==null){ println("  ["+tag+"] no fn @ 0x"+Long.toHexString(rva)); return; }
        DecompileResults r = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        println("\n=== ["+tag+"] "+f.getName()+" @ "+f.getEntryPoint()+" size="+f.getBody().getNumAddresses()+" ===");
        if (r!=null && r.decompileCompleted()){ int i=0; for(String ln: r.getDecompiledFunction().getC().split("\n")){ println(String.format("  %3d: %s", i++, ln)); if(i>75){println("  ...trunc");break;} } }
        else println("  decompile failed");
        Reference[] refs = getReferencesTo(f.getEntryPoint());
        println("  direct refs to fn entry: " + refs.length);
        int sh=0; for(Reference rf: refs){ if(sh++>=12)break; Function c=getFunctionContaining(rf.getFromAddress()); println("    <- "+rf.getFromAddress()+" in "+(c!=null?c.getName():"<no fn>")); }
    }
    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace(); mem=currentProgram.getMemory();
        di=new DecompInterface(); di.openProgram(currentProgram);
        for(MemoryBlock b: mem.getBlocks()){ if(b.isExecute()){ if(textLo==0)textLo=b.getStart().getOffset(); long e=b.getEnd().getOffset(); if(e>textHi)textHi=e; } }
        Address vt=sp.getAddress(VT);
        println("=== vtable 0x"+Long.toHexString(VT)+" slots ===");
        List<Long> slots=new ArrayList<>();
        for(int i=0;i<14;i++){ long s; try{ s=mem.getLong(vt.add(8L*i)); }catch(Exception e){break;} if(!inText(s)){ println(String.format("  slot[%d]=0x%X (not .text, stop)",i,s)); break; } slots.add(s); Function f=getFunctionContaining(sp.getAddress(s)); println(String.format("  slot[%d]=0x%X %s",i,s,f!=null?f.getName():"<no fn>")); }
        // Decompile every slot - the property is small, all slots matter (get/set/serialize/notify).
        for(int i=0;i<slots.size();i++) decomp(slots.get(i), "slot["+i+"]");
        println("\ndone.");
    }
}
