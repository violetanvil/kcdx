// FRONT F3: trace the CGF chunk-file READER's open/read leaf.
// C3DEngine::LoadChunkFileContent (FUN_18051ce88) delegates to the chunk-file reader
// FUN_180980d4c (init'd by FUN_180980e18). The file OPEN is inside that reader (or a callee).
// Decompile a fixed set of reader/leaf functions FULLY, and for each flag any call to the
// verified open/read/stream targets so the lane is READ at the call site, not inferred.
// Image base 0x180000000. Read-only. Mirrors DumpFOpenHandleConstruct.java.
//@category KCD2

import java.util.LinkedHashMap;
import java.util.Map;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class ModelChunkReaderTrace extends GhidraScript {

    // RVA -> human note. The chunk-reader chain + the .cdf/character CreateInstance path.
    private static final String[][] TGT = {
        {"0x180980d4c", "chunk-file reader (LoadChunkFileContent delegate)"},
        {"0x180980e18", "chunk-reader init (CLoadLogListener install)"},
        {"0x18052c4f8", "LoadStatObj default-cgf loader"},
        {"0x18048c838", "CGF content instance ctor (param_5,param_4)"},
        {"0x180980c64", "chunk-reader teardown"},
    };

    // Verified open/read/stream targets to flag in any body.
    private static final String[][] LANES = {
        {"FUN_1804605bc", "CCryFile::Open(0x4605BC) FOpen-loose"},
        {"FUN_1804614a0", "ICryPak::FOpen(slot36) loose"},
        {"FUN_182418de4", "FOpenRaw(slot35)"},
        {"FUN_180461304", "FReopen/FOpen-by-pak-index(slot38)"},
        {"FUN_180460b08", "CCryFile::ReadRaw(0x460B08)->slot38/OS"},
        {"FUN_18051cd00", "FGetCachedFileData(slot40)"},
        {"FUN_18051e1f8", "FReadRaw(slot39)"},
        {"FUN_180461a5c", "ZipDir FRead/FSeek fallback(0x461A5C)"},
        {"FUN_182418b48", "GetFileSize-by-name(slot45) SIZE-GATE"},
        {"FUN_180463ec4", "IsFileExist-3arg(slot67) EXIST-GATE"},
        {"FUN_18241abcc", "IsFileExist-2arg(slot70) EXIST-GATE"},
        {"FUN_1804647fc", "StreamAsyncFileRequest(0x4647FC) MOUNT/STREAM"},
        {"FUN_180464b88", "ZipDir::ReadFileStreaming(0x464B88) MOUNT/STREAM read"},
        {"FUN_1804d6910", "ZipDir uncached CreateFileA opener(0x4D6910) MOUNT"},
        {"FUN_1809b2b28", "CryPak _wfopen producer(0x9B2B28)"},
        {"fopen", "CRT fopen"}, {"_wfopen", "CRT _wfopen"}, {"CreateFile", "WIN32 CreateFile"},
        {"CreateFileMapping", "mmap"}, {"MapViewOfFile", "mmap"},
        {"FGetCachedFileData", "FGetCachedFileData(named)"},
    };

    private void emit(long rva, String note, DecompInterface di) throws Exception {
        Address a = currentProgram.getImageBase().add(rva - 0x180000000L);
        Function f = getFunctionContaining(a);
        println("\n================================================================================");
        if (f == null) { println("NO FUNCTION at 0x" + Long.toHexString(rva) + " (" + note + ")"); return; }
        println("FUNC " + f.getName() + " @ 0x" + Long.toHexString(f.getEntryPoint().getOffset())
            + "  note=" + note + "  size=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 120, monitor);
        if (dr == null || !dr.decompileCompleted()) { println("  <decompile failed>"); return; }
        String c = dr.getDecompiledFunction().getC();
        StringBuilder lanes = new StringBuilder();
        for (String[] t : LANES) if (c.contains(t[0])) lanes.append(t[1]).append(" [").append(t[0]).append("] | ");
        if (c.contains("+ 0x120)")) lanes.append("vt+0x120(FOpen slot36) | ");
        if (c.contains("+ 0x130)")) lanes.append("vt+0x130(slot38) | ");
        if (c.contains("+ 0x118)")) lanes.append("vt+0x118(FOpenRaw slot35) | ");
        if (c.contains("+ 0x168)")) lanes.append("vt+0x168(GetFileSize slot45)SIZE | ");
        if (c.contains("+ 0x218)")) lanes.append("vt+0x218(IsFileExist slot67)EXIST | ");
        println("  LANES: " + (lanes.length()==0 ? "(none named)" : lanes.toString()));
        for (String ln : c.split("\n")) println("    " + ln);
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String[] t : TGT) emit(Long.parseLong(t[0].substring(2), 16), t[1], di);
        di.dispose();
        println("\ndone.");
    }
}
