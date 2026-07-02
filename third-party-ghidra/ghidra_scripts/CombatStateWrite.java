// Find the combat-state WRITER. Getter FUN_181a7dac0 = return *(byte*)(prop+8). prop vtable 0x183b091e0.
// FUN_1830312c0 calls the getter twice (a state-consumer, possibly the SM). The setter writes [prop+8] + fires signal.
// This script: decompiles getter-consumers that may also be the transition SM, and scans .text for the
// SetValue shape (mov byte [reg+8], v ; <compare> ; call) by decompiling functions that REFERENCE the bool-prop vtable
// or write a bool into a +8 offset on an object whose +0 is this vtable.
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

public class CombatStateWrite extends GhidraScript {
    long textLo=0,textHi=0; Memory mem; AddressSpace sp; DecompInterface di;
    boolean inText(long v){return v>=textLo&&v<textHi;}
    void decomp(long va,String tag,int max) throws Exception {
        Address a=sp.getAddress(va); Function f=getFunctionContaining(a);
        if(f==null){println("\n["+tag+"] no fn @ 0x"+Long.toHexString(va));return;}
        DecompileResults r=di.decompileFunction(f,60,new ConsoleTaskMonitor());
        println("\n=== ["+tag+"] "+f.getName()+" @ "+f.getEntryPoint()+" size="+f.getBody().getNumAddresses()+" ===");
        if(r!=null&&r.decompileCompleted()){int i=0;for(String ln:r.getDecompiledFunction().getC().split("\n")){println(String.format("  %3d: %s",i++,ln));if(i>max){println("  ...trunc");break;}}}
        else println("  decompile failed: "+(r!=null?r.getErrorMessage():"null"));
    }
    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace(); mem=currentProgram.getMemory();
        di=new DecompInterface(); di.openProgram(currentProgram);
        for(MemoryBlock b:mem.getBlocks()){if(b.isExecute()){if(textLo==0)textLo=b.getStart().getOffset();long e=b.getEnd().getOffset();if(e>textHi)textHi=e;}}
        // 1. The named getter-consumer that calls the getter twice.
        decomp(0x1830312c0L,"getter-consumer FUN_1830312c0", 80);
        // 2. The combat-actor model ctor caller (owns the model) - find the owning object layout.
        decomp(0x18091dabcL,"model-ctor-caller @0x18091dabc", 60);
        // 3. Standalone bool-prop init owner.
        decomp(0x1810ee7f0L,"boolprop-init-owner @0x1810ee7f0", 50);
        println("\ndone.");
    }
}
