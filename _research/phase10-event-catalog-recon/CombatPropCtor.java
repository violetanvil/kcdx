// Decompile the two combat-bool C_ModelProperty constructor sites + locate the inlined SetValue/notify.
// Construction (lea VT) sites: 0x1810ef2b1, 0x1810efea4 (RVA 0x10ef2b1 / 0x10efea4). Getter reads [prop+8]; signal sub-object at [prop+0x18].
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

public class CombatPropCtor extends GhidraScript {
    long textLo=0,textHi=0; Memory mem; AddressSpace sp; DecompInterface di;
    boolean inText(long v){ return v>=textLo&&v<textHi; }
    void decomp(long rva,String tag) throws Exception {
        Address a=sp.getAddress(rva); Function f=getFunctionContaining(a);
        if(f==null){ println("\n["+tag+"] no fn @ 0x"+Long.toHexString(rva)); return; }
        DecompileResults r=di.decompileFunction(f,60,new ConsoleTaskMonitor());
        println("\n=== ["+tag+"] "+f.getName()+" @ "+f.getEntryPoint()+" size="+f.getBody().getNumAddresses()+" (req-site 0x"+Long.toHexString(rva)+") ===");
        if(r!=null&&r.decompileCompleted()){ int i=0; for(String ln:r.getDecompiledFunction().getC().split("\n")){ println(String.format("  %3d: %s",i++,ln)); if(i>90){println("  ...trunc");break;} } }
        else println("  decompile failed");
    }
    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace(); mem=currentProgram.getMemory();
        di=new DecompInterface(); di.openProgram(currentProgram);
        for(MemoryBlock b:mem.getBlocks()){ if(b.isExecute()){ if(textLo==0)textLo=b.getStart().getOffset(); long e=b.getEnd().getOffset(); if(e>textHi)textHi=e; } }
        decomp(0x1810ef2b1L,"ctor-site-A");
        decomp(0x1810efea4L,"ctor-site-B");
        println("\ndone.");
    }
}
