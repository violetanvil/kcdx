// asset-loadpath-map-recon F5 step3: settle the HANDLE SOURCE for streaming reads.
// ZipDir::ReadFileStreaming (FUN_180464b88) reads from a HANDLE at [zipDirObj+0x10]
// (m_zipFile), gated by sys_UncachedStreamReads (DAT_1849272b8). The question: is
// that HANDLE the pak's mount handle (opened by CCryPak's mount path → no FOpen
// bypass for the OPEN) or an INDEPENDENT CreateFile (a true bypass)?
//
// Method: find every write to a struct field at +0x10 in functions that also
// touch the ZipDir cache, by scanning the bypass-candidate CreateFile callers in
// the 0x46xxxx / 0x4dxxxx (CryPak/ZipDir) band and dumping their bodies + the
// uncached-handle cvar DAT_1849272b8 referencing fns. Also dump the two raw
// CreateFile producers FUN_182423e08 (FOpen & 0x10 path) + FUN_1804d6910 +
// FUN_18192d410 (the band's CreateFileA callers) to see WHO opens the stream handle.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.*;
import java.io.*;

public class ZipDirHandleSource extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    PrintWriter out;
    Address a(long va){ return sp.getAddress(va); }
    void w(String s){ out.println(s); }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);
        String dst = "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\asset-loadpath-map-recon\\_ziphandle_source.txt";
        out = new PrintWriter(new FileWriter(dst));

        // who references the uncached-stream-reads cvar value DAT_1849272b8?
        long unc = 0x1849272b8L;
        w("########## refs to DAT_1849272b8 (sys_UncachedStreamReads value) ##########");
        Reference[] urefs = getReferencesTo(a(unc));
        LinkedHashSet<Long> uncFns = new LinkedHashSet<>();
        for (Reference rf : urefs) {
            Function cf = getFunctionContaining(rf.getFromAddress());
            if (cf!=null) uncFns.add(cf.getEntryPoint().getOffset());
        }
        for (long f : uncFns) w("  fn 0x"+Long.toHexString(f));

        // The CreateFile callers in the CryPak/ZipDir band + the FOpen & 0x10 producer
        long[] producers = {
            0x182423e08L, // FOpen "& 0x10" path producer (CreateFileA -> CRT FILE*) -- front says CRT FILE*
            0x1804d6910L, // band CreateFileA caller
            0x18192d410L, // band CreateFileA caller
            0x182476768L  // band CreateFileA caller
        };
        for (long t : producers) {
            Function f = getFunctionAt(a(t));
            if (f==null){ w("\n!!! no fn at 0x"+Long.toHexString(t)); continue; }
            w("\n==================================================================");
            w("FN "+f.getName()+" @0x"+Long.toHexString(t));
            Reference[] refs = getReferencesTo(f.getEntryPoint());
            LinkedHashSet<String> callers = new LinkedHashSet<>();
            for (Reference rf : refs) {
                Function cf = getFunctionContaining(rf.getFromAddress());
                if (cf!=null) callers.add(cf.getName()+"@0x"+Long.toHexString(cf.getEntryPoint().getOffset()));
            }
            w("CALLERS ("+callers.size()+"): "+callers);
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            if (r==null||!r.decompileCompleted()){ w("(decompile failed)"); continue; }
            w("------------------------------------------------------------------");
            w(r.getDecompiledFunction().getC());
        }
        out.flush(); out.close();
        println("wrote zip-handle-source dump. done.");
    }
}
