// FRONT F1 part 3: read the StreamEngine read primitive bodies to decide whether the
// (non-DirectStorage) texture stream read reaches CCryPak FOpen(+0x120)/FReadRaw(slot39)/
// FGetCachedFileData(slot40)/GetFileSize(slot45) — or DirectStorage / its own file I/O.
// Discovery proved: no CreateFile*/ReadFile/MapViewOfFile refs in main binary; engine uses
// a StreamEngine with a DirectStorage path + a "normal stream engine" fallback.
//
// Anchors:
//   FUN_1804647fc = StreamAsyncFileRequest.cpp  (the async file request)
//   FUN_1804898d0 = StreamEngine.cpp            (the stream engine)
//   FUN_180d2ad38 = DirectStorage-vs-normal selector ("Fallbacking to normal stream engine")
//   FUN_180d2a2b8 = CStreamEngine ctor/init
// For each: decompile + scan for the CCryPak vtable offsets +0x120(FOpen36) +0x138(FReadRaw39)
//   +0x140(FGetCachedFileData40) +0x168(GetFileSize45) +0x218(IsFileExist67), and for
//   dstorage / pCryPak (DAT_18492b850).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class StreamEngineReadPrim extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void scan(String c) {
        // flag any CCryPak read/open vtable offset and pak/dstorage tokens present in the body
        String[][] marks = {
            {"+ 0x120","FOpen(slot36)"}, {"0x120))","FOpen(slot36)"},
            {"+ 0x138","FReopen/FReadRaw(slot38/39)"},
            {"+ 0x140","FGetCachedFileData(slot40)"},
            {"+ 0x168","GetFileSize(slot45)"},
            {"+ 0x218","IsFileExist(slot67)"},
            {"DAT_18492b850","pCryPak(gEnv+0x50)"},
            {"DirectStorage","dstorage-token"}, {"dstorage","dstorage-token"},
            {"FUN_18051cd00","FGetCachedFileData-body"},
            {"FUN_1804614a0","FOpen-body"}, {"FUN_1804605bc","CCryFile::Open"},
            {"FUN_18051e1f8","FReadRaw-body"}, {"FUN_1804d7ab4","CRT-fread-leaf"},
            {"_wfopen","wfopen"}, {"fopen","fopen"}, {"ReadFile","Win32-ReadFile"},
            {"CreateFile","Win32-CreateFile"},
        };
        println("  --- offset/token scan ---");
        boolean any=false;
        for (String[] m : marks) {
            int idx = c.indexOf(m[0]);
            if (idx >= 0) {
                any=true;
                int cnt=0, from=0;
                while ((from=c.indexOf(m[0],from))>=0){cnt++;from+=m[0].length();}
                println("    HIT " + m[1] + "  token=\""+m[0]+"\" x"+cnt);
            }
        }
        if (!any) println("    (no CCryPak read/open offset, no pak global, no dstorage/Win32-IO token in body)");
    }

    void dump(long fva, String label) throws Exception {
        Function f = getFunctionAt(a(fva));
        if (f == null) f = getFunctionContaining(a(fva));
        println("\n================================================================================");
        println(label + " @0x" + Long.toHexString(fva));
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION"); return; }
        println("  proto: " + f.getPrototypeString(true,false) + "  size(addrs)=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 240, new ConsoleTaskMonitor());
        if (dr == null || !dr.decompileCompleted()) { println("  (decompile failed: " + (dr!=null?dr.getErrorMessage():"null") + ")"); return; }
        String c = dr.getDecompiledFunction().getC();
        scan(c);
        println("  --- body ---");
        println(c);
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        dump(0x180d2ad38L, "DirectStorage-vs-normal selector ('Fallbacking to normal stream engine')");
        dump(0x180d2a2b8L, "CStreamEngine init FUN_180d2a2b8");
        dump(0x1804898d0L, "StreamEngine.cpp FUN_1804898d0");
        dump(0x1804647fcL, "StreamAsyncFileRequest.cpp FUN_1804647fc");

        println("\ndone.");
    }
}
