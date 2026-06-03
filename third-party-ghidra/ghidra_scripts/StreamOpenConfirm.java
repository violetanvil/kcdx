// FRONT F1 part 6 (final edge): confirm the texture-stream OPEN site goes through
// CCryFile::Open->FOpen, and characterize FUN_180460b64 (size-on-handle vs pre-open gate).
//   FUN_1807b5ed4 = the open site (CCryFile::Open caller, builds request struct)
//   FUN_180460b64 = called in the async caller right after open (CCryFile::GetLength?)
//@category KCD2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class StreamOpenConfirm extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    void dump(long fva,String label) throws Exception {
        Function f=getFunctionAt(a(fva)); if(f==null) f=getFunctionContaining(a(fva));
        println("\n================ "+label+" @0x"+Long.toHexString(fva)+" ================");
        if(f==null){ println("  NO FUNCTION"); return; }
        println("  size(addrs)="+f.getBody().getNumAddresses());
        DecompileResults dr=di.decompileFunction(f,240,new ConsoleTaskMonitor());
        if(dr!=null&&dr.decompileCompleted()) println(dr.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }
    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace();
        di=new DecompInterface(); di.openProgram(currentProgram);
        dump(0x1807b5ed4L,"texture-stream OPEN site FUN_1807b5ed4 (CCryFile::Open caller)");
        dump(0x180460b64L,"FUN_180460b64 (size-on-handle? CCryFile::GetLength?)");
        println("\ndone.");
    }
}
