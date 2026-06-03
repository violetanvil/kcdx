// FRONT F3: resolve CReadOnlyChunkFile::Read (the model-file OPEN site) and read its lane.
// In CLoaderCGF::LoadCGF (FUN_180980d4c) the open is the virtual call (*param_4 + 0x20)(param_4,path)
// on a CReadOnlyChunkFile (vftable set in FUN_18048c838). Resolve that vtable symbol, decompile
// its slots +0x18 (3) and +0x20 (4), and the CGF-parse fallback FUN_1806a24e4. Flag any verified
// open/read/stream target named in each body, so the lane is READ at the call site.
// Image base 0x180000000. Read-only.
//@category KCD2

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class ChunkFileReadOpenSite extends GhidraScript {

    private static final String[][] LANES = {
        {"FUN_1804605bc", "CCryFile::Open(0x4605BC) FOpen-loose"},
        {"FUN_1804614a0", "ICryPak::FOpen(slot36) loose"},
        {"FUN_182418de4", "FOpenRaw(slot35)"},
        {"FUN_180461304", "FReopen/FOpen-by-pak-index(slot38)"},
        {"FUN_180460b08", "CCryFile::ReadRaw(0x460B08)->slot38/OS"},
        {"FUN_18051cd00", "FGetCachedFileData(slot40,0x51CD00)"},
        {"FUN_18051e1f8", "FReadRaw(slot39)"},
        {"FUN_182418b48", "GetFileSize-by-name(slot45) SIZE-GATE"},
        {"FUN_180463ec4", "IsFileExist-3arg(slot67) EXIST-GATE"},
        {"FUN_18241abcc", "IsFileExist-2arg(slot70) EXIST-GATE"},
        {"FUN_1804647fc", "StreamAsyncFileRequest(0x4647FC) MOUNT/STREAM"},
        {"FUN_180464b88", "ZipDir::ReadFileStreaming(0x464B88) MOUNT/STREAM read"},
        {"FUN_1804d6910", "ZipDir uncached CreateFileA opener(0x4D6910) MOUNT"},
        {"FUN_1809b2b28", "CryPak _wfopen producer(0x9B2B28)"},
        {"FGetCachedFileData", "FGetCachedFileData(named)"},
        {"fopen_s", "CRT fopen_s"}, {"_wfopen", "CRT _wfopen"},
        {"CreateFileMapping", "mmap"}, {"MapViewOfFile", "mmap"}, {"CreateFile", "WIN32 CreateFile"},
    };

    private DecompInterface di;

    private void dump(String label, Function f) throws Exception {
        println("\n================================================================================");
        if (f == null) { println(label + " -> NO FUNCTION"); return; }
        println(label + " -> " + f.getName() + " @ 0x" + Long.toHexString(f.getEntryPoint().getOffset())
            + "  size=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 120, monitor);
        if (dr == null || !dr.decompileCompleted()) { println("  <decompile failed>"); return; }
        String c = dr.getDecompiledFunction().getC();
        StringBuilder lanes = new StringBuilder();
        for (String[] t : LANES) if (c.contains(t[0])) lanes.append(t[1]).append(" [").append(t[0]).append("] | ");
        if (c.contains("+ 0x120)")) lanes.append("vt+0x120(FOpen slot36) | ");
        if (c.contains("+ 0x130)")) lanes.append("vt+0x130(slot38) | ");
        if (c.contains("+ 0x168)")) lanes.append("vt+0x168(GetFileSize slot45)SIZE | ");
        if (c.contains("+ 0x218)")) lanes.append("vt+0x218(IsFileExist slot67)EXIST | ");
        println("  LANES: " + (lanes.length()==0 ? "(none named)" : lanes.toString()));
        for (String ln : c.split("\n")) println("    " + ln);
    }

    private Function fnAtPtr(Address vt, long off) throws Exception {
        Address slot = vt.add(off);
        long target = getLong(slot) & 0xFFFFFFFFFFFFL | 0x180000000000L; // not used; use getDataAt
        Address fp = toAddr(getLong(slot));
        return getFunctionAt(fp);
    }

    @Override
    public void run() throws Exception {
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // Find the CReadOnlyChunkFile vtable symbol. Match on the FULLY-QUALIFIED name
        // (namespace::name) since the bare getName() is just "vftable".
        Address vt = null;
        SymbolIterator all = currentProgram.getSymbolTable().getAllSymbols(true);
        while (all.hasNext()) {
            Symbol s = all.next();
            String full = s.getName(true); // includes namespace path
            if (full.contains("CReadOnlyChunkFile") && (full.contains("vftable") || full.contains("vtable"))) {
                vt = s.getAddress(); println("FOUND vtable symbol: " + full + " @ " + vt); break;
            }
        }
        if (vt != null) {
            // The symbol resolved is the RTTI meta-pointer (vftable[-1]); the object stores the
            // vftable START, which is meta_ptr_addr + 8. The call (*obj + 0xN) indexes from there.
            Address vbase = vt.add(8);
            println("vftable START (meta_ptr+8) = " + vbase);
            for (long off : new long[]{0x0,0x8,0x10,0x18,0x20,0x28,0x30,0x38}) {
                Address fp = toAddr(getLong(vbase.add(off)));
                Function f = getFunctionAt(fp);
                String nm = (f != null) ? f.getName() : "(no fn)";
                println("  vftable +0x" + Long.toHexString(off) + " -> " + fp + "  " + nm);
            }
            dump("CReadOnlyChunkFile vftable+0x18 (3)", getFunctionAt(toAddr(getLong(vbase.add(0x18)))));
            dump("CReadOnlyChunkFile vftable+0x20 (4) = the OPEN/Read (*obj+0x20)", getFunctionAt(toAddr(getLong(vbase.add(0x20)))));
        } else {
            println("CReadOnlyChunkFile vtable symbol NOT FOUND");
        }

        // The CGF-parse fallback in LoadCGF.
        dump("FUN_1806a24e4 (CGF parse from chunk file)", getFunctionAt(toAddr(0x1806a24e4L)));

        di.dispose();
        println("\ndone.");
    }
}
