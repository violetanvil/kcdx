// asset-loadpath-map-recon F5 step4: confirm FUN_1804d6910 (the uncached
// CreateFileA that mints the streaming m_zipFile handle) is reached from the pak
// MOUNT path (archive factory slot 72 FUN_1804d5580 / its RW arm FUN_1804d5b74),
// i.e. the streaming handle is opened DURING CCryPak pak mount, not independently.
// Dump FUN_1804d5b74 (the caller of the uncached opener) + its callers, and dump
// FUN_180461a5c (the NON-streaming fallback read leaf the streaming leaf falls
// back to, to confirm it is CCryPak FReadRaw-family).
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

public class ZipUncachedOpenChain extends GhidraScript {
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
        String dst = "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\asset-loadpath-map-recon\\_zip_uncached_chain.txt";
        out = new PrintWriter(new FileWriter(dst));

        long[] targets = {
            0x1804d5b74L, // RW-arm of the ZipDir cache builder (caller of the uncached opener)
            0x180461a5cL  // streaming-leaf non-streaming fallback read (expect CCryPak FReadRaw-family)
        };
        for (long t : targets) {
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
        println("wrote uncached-chain dump. done.");
    }
}
