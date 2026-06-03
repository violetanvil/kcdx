// FRONT F1 part 4: the StreamAsyncFileRequest body (FUN_1804647fc) operates on a handle
// at param_3+0x108 (the CCryFile handle offset per front3). Find WHERE that file is opened
// and what the read leaves are. Decompile:
//   FUN_180463984 = pak-entry/archive lookup on pCryPak (called with param_3+0x108)
//   FUN_180463abc = sibling (GetArchivePath family, front1 slot 93 leaf)
//   FUN_180460b08 = the loose/handle read leaf (local_70==0 branch)
//   FUN_180464b88 = the pak read leaf (local_70!=0 branch)
//   FUN_18245839c = buffer-acquire (param_1)
// AND: who CALLS FUN_1804647fc, and does the caller open via CCryPak::FOpen? Walk callers up
//   one level and scan each for +0x120 (FOpen36)/CCryFile::Open(FUN_1804605bc)/FUN_1804614a0.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class StreamReqOpenLeaves extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    String fnLabel(Address from){ Function cf=getFunctionContaining(from); return cf==null?("<no-fn>@"+from):(cf.getName()+" @0x"+Long.toHexString(cf.getEntryPoint().getOffset())); }

    String decomp(long fva){
        try {
            Function f=getFunctionAt(a(fva)); if(f==null) f=getFunctionContaining(a(fva));
            if(f==null) return null;
            DecompileResults dr=di.decompileFunction(f,240,new ConsoleTaskMonitor());
            if(dr==null||!dr.decompileCompleted()) return null;
            return dr.getDecompiledFunction().getC();
        } catch(Exception e){ return null; }
    }

    void scanOpen(String c, String label){
        String[][] marks={{"+ 0x120","FOpen(slot36)"},{"0x120))","FOpen(slot36)"},
            {"FUN_1804614a0","FOpen-body"},{"FUN_1804605bc","CCryFile::Open"},
            {"FUN_1804647fc","StreamAsyncFileRequest"},{"FUN_180461304","FOpenByPakIdx(slot38body)"},
            {"_wfopen","wfopen"},{"+ 0x108","handle@+0x108"},{"+ 0x168","GetFileSize(slot45)"},
            {"+ 0x218","IsFileExist(slot67)"},{"DAT_18492b850","pCryPak"}};
        StringBuilder sb=new StringBuilder("  scan["+label+"]: ");
        boolean any=false;
        for(String[] m:marks){ if(c.contains(m[0])){ any=true; sb.append(m[1]).append(" "); } }
        if(!any) sb.append("(none)");
        println(sb.toString());
    }

    void dump(long fva,String label) {
        println("\n================ " + label + " @0x"+Long.toHexString(fva)+" ================");
        String c=decomp(fva);
        if(c==null){ println("  (no decomp)"); return; }
        Function f=getFunctionAt(a(fva)); if(f==null) f=getFunctionContaining(a(fva));
        println("  size(addrs)="+(f!=null?f.getBody().getNumAddresses():-1));
        scanOpen(c,label);
        println(c);
    }

    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace();
        di=new DecompInterface(); di.openProgram(currentProgram);

        // leaves of the request body
        dump(0x180463984L,"FUN_180463984 pak-entry lookup (called w/ handle param_3+0x108)");
        dump(0x180460b08L,"FUN_180460b08 read leaf (loose/local_70==0 branch)");
        dump(0x180464b88L,"FUN_180464b88 read leaf (pak/local_70!=0 branch)");

        // who calls the request? walk up one level, scan each caller for the OPEN
        println("\n########## callers of StreamAsyncFileRequest FUN_1804647fc ##########");
        Reference[] rs=getReferencesTo(a(0x1804647fcL));
        LinkedHashSet<Long> callers=new LinkedHashSet<>();
        for(Reference rf:rs){ println("  <- "+fnLabel(rf.getFromAddress())+" ("+rf.getReferenceType()+")"); Function cf=getFunctionContaining(rf.getFromAddress()); if(cf!=null) callers.add(cf.getEntryPoint().getOffset()); }
        for(long c:callers){
            String body=decomp(c);
            if(body!=null) scanOpen(body, "caller 0x"+Long.toHexString(c));
        }

        // also: who opens a stream file? find the StreamEngine StartRead entry that mints the handle.
        // The request's handle sits at param_3+0x108 — find functions that WRITE +0x108 near a FOpen call.
        println("\n########## FOpen(0x4614A0) callers that also touch +0x108 (handle mint sites) ##########");
        Reference[] fo=getReferencesTo(a(0x1804614a0L));
        println("  FOpen direct refs: "+fo.length);
        LinkedHashSet<Long> foFns=new LinkedHashSet<>();
        for(Reference rf:fo){ Function cf=getFunctionContaining(rf.getFromAddress()); if(cf!=null) foFns.add(cf.getEntryPoint().getOffset()); }
        int shown=0;
        for(long c:foFns){
            String body=decomp(c);
            if(body!=null && body.contains("0x108")){
                println("  HANDLE-MINT? "+fnLabel(a(c))+" calls FOpen AND touches +0x108");
                if(++shown>=8) break;
            }
        }
        println("\ndone.");
    }
}
