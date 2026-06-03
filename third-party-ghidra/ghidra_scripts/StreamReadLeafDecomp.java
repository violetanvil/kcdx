// asset-loadpath-map-recon F5 step2: decompile the streaming read leaves to read
// the HANDLE SOURCE. The question: ZipDir::ReadFileStreaming (FUN_180464b88) calls
// ReadFile directly — but on WHAT handle? Is m_zipFile a handle minted by CCryPak's
// pak mount (so streaming reads the SAME mounted-pak OS handle => no FOpen bypass for
// the OPEN, only a faster read of an already-CCryPak-opened file), or does the
// streaming path open its own file independent of CCryPak?
//
// Dump: full decompile of the streaming read leaf + its caller(s) + the
// "File:'%s', pak:'%s'" streaming-read fn, and list callers of the leaf.
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

public class StreamReadLeafDecomp extends GhidraScript {
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
        String dst = "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\asset-loadpath-map-recon\\_streamread_leaf_decomp.txt";
        out = new PrintWriter(new FileWriter(dst));

        long[] targets = {
            0x180464b88L, // ZipDir::ReadFileStreaming (ReadFile+SetFilePointer leaf)
            0x1804647fcL, // "Error: Streaming read failed. File:'%s', pak:'%s'" — streaming read caller
            0x18048925cL, // "Use of the stream engine without a file is deprecated"
            0x1804898d0L  // CryEngine StreamEngine anchor
        };
        for (long t : targets) {
            Function f = getFunctionAt(a(t));
            if (f == null) { w("\n!!! no fn at 0x"+Long.toHexString(t)); continue; }
            w("\n==================================================================");
            w("FN "+f.getName()+" @0x"+Long.toHexString(t));
            // list callers
            Reference[] refs = getReferencesTo(f.getEntryPoint());
            LinkedHashSet<String> callers = new LinkedHashSet<>();
            for (Reference rf : refs) {
                Function cf = getFunctionContaining(rf.getFromAddress());
                if (cf!=null) callers.add(cf.getName()+"@0x"+Long.toHexString(cf.getEntryPoint().getOffset()));
            }
            w("CALLERS ("+callers.size()+"): "+callers);
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            if (r==null || !r.decompileCompleted()) { w("(decompile failed)"); continue; }
            w("------------------------------------------------------------------");
            w(r.getDecompiledFunction().getC());
        }
        out.flush(); out.close();
        println("wrote decompile dump. done.");
    }
}
