// FRONT F1 part 5: find the OPEN site for the texture stream request's file.
// Established: the request (FUN_1804647fc) reads via CCryFile::ReadRaw (FUN_180460b08):
//   ICryPak bound -> ICryPak vtable+0x130 (slot38 read-family) on handle@CCryFile+0x108;
//   else raw fread on FILE*@+0x108. The handle/CCryFile is opened upstream.
// CCryFile::Open = FUN_1804605bc (front3 id) routes to ICryPak::FOpen(+0x120) or fopen_s.
// QUESTION: does the texture-stream caller open via CCryFile::Open(FUN_1804605bc) / FOpen,
//   and is there a GetFileSize(slot45,+0x168)/IsFileExist(slot67,+0x218) check before the read?
//
// Approach: decompile the request callers FUN_1807b54b4 / FUN_1807b5d58 (CTexture stream),
//   and ALL CCryFile::Open (FUN_1804605bc) callers in the 0x806x/0x807bx stream cluster +
//   scan for FOpen / size / exist. Also decompile FUN_180464b88 (pak read leaf) for completeness.
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

public class StreamOpenSite extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    String fnLabel(Address from){ Function cf=getFunctionContaining(from); return cf==null?("<no-fn>@"+from):(cf.getName()+" @0x"+Long.toHexString(cf.getEntryPoint().getOffset())); }

    String decomp(long fva){
        try { Function f=getFunctionAt(a(fva)); if(f==null) f=getFunctionContaining(a(fva)); if(f==null) return null;
            DecompileResults dr=di.decompileFunction(f,240,new ConsoleTaskMonitor());
            return (dr!=null&&dr.decompileCompleted())?dr.getDecompiledFunction().getC():null;
        } catch(Exception e){ return null; }
    }
    void scan(String c,String label){
        String[][] marks={{"+ 0x120","FOpen(slot36)"},{"0x120))","FOpen(slot36)"},
            {"FUN_1804614a0","FOpen-body"},{"FUN_1804605bc","CCryFile::Open"},
            {"+ 0x168","GetFileSize(slot45)"},{"0x168))","GetFileSize(slot45)"},
            {"+ 0x218","IsFileExist(slot67)"},{"0x218))","IsFileExist(slot67)"},
            {"+ 0x1b0","GetFileSizeUncomp(slot92?)"},{"+ 0x130))","CCryFileReadRaw->slot38"},
            {"FUN_1804647fc","StreamReq"},{"_wfopen","wfopen"},{"fopen_s","fopen_s"},
            {"DAT_18492b850","pCryPak"}};
        StringBuilder sb=new StringBuilder("  scan["+label+"]: "); boolean any=false;
        for(String[] m:marks){ if(c.contains(m[0])){ any=true; sb.append(m[1]).append(" "); } }
        println(sb.append(any?"":"(none)").toString());
    }
    void dump(long fva,String label,boolean full){
        println("\n================ "+label+" @0x"+Long.toHexString(fva)+" ================");
        String c=decomp(fva); if(c==null){ println("  (no decomp)"); return; }
        Function f=getFunctionAt(a(fva)); if(f==null) f=getFunctionContaining(a(fva));
        println("  size(addrs)="+(f!=null?f.getBody().getNumAddresses():-1));
        scan(c,label);
        if(full) println(c);
    }

    @Override public void run() throws Exception {
        sp=currentProgram.getAddressFactory().getDefaultAddressSpace();
        di=new DecompInterface(); di.openProgram(currentProgram);

        // the texture-stream request callers (where the request struct param_3 is built/opened)
        dump(0x1807b54b4L,"CTexture stream caller FUN_1807b54b4",true);
        dump(0x1807b5d58L,"CTexture stream caller FUN_1807b5d58",true);

        // all CCryFile::Open callers — which are in the stream/texture cluster, and do any precede with a size/exist check
        println("\n########## CCryFile::Open (FUN_1804605bc) callers — stream/texture cluster scan ##########");
        Reference[] rs=getReferencesTo(a(0x1804605bcL));
        println("  total refs: "+rs.length);
        LinkedHashSet<Long> callers=new LinkedHashSet<>();
        for(Reference rf:rs){ Function cf=getFunctionContaining(rf.getFromAddress()); if(cf!=null) callers.add(cf.getEntryPoint().getOffset()); }
        for(long c:callers){
            String body=decomp(c); if(body==null) continue;
            // only report callers that look texture/stream-related OR that call FOpen/size/exist
            boolean tex = (c>=0x180860000L && c<=0x1808a0000L) || (c>=0x1807b0000L && c<=0x1807c0000L)
                          || body.contains("FUN_1804647fc") || body.contains("Texture");
            if(tex) scan(body,"CCryFileOpenCaller 0x"+Long.toHexString(c));
        }

        // FUN_180464b88 pak read leaf body (the local_70!=0 branch) for completeness
        dump(0x180464b88L,"FUN_180464b88 pak read leaf",true);

        println("\ndone.");
    }
}
