// FRONT F3 (asset-loadpath-map): the .cgf/.cdf MODEL load path in WHGame.dll.
// One lock-hold pass. Goal: find the model loader's OPEN site and READ which lane it uses —
//   (a) FOpen loose lane:  CCryFile::Open (FUN_1804605bc) -> ICryPak::FOpen (slot36 0x4614A0)
//                          + ReadRaw (FUN_180460b08) -> slot38 / OS-arm fread on handle@+0x108
//   (b) mount/streaming lane: hands path to StreamAsyncFileRequest (FUN_1804647fc) which reads
//                          via ZipDir::ReadFileStreaming (FUN_180464b88) on the mount-minted
//                          CreateFileA handle (archive factory slot72 FUN_1804d5580/FUN_1804d6910).
//   (c) mmap: ruled out engine-wide (zero CreateFileMapping/MapViewOfFile imports) — not probed.
// Steps: (1) .cgf/.cdf/.chr/.skin/.cga/.caf + CGF/CStatObj/Chunk loader-class string xrefs ->
// referencing fns; (2) per fn, detect calls to the verified open/read/stream targets BY NAME in
// the decompiled body so the lane is READ not inferred; (3) emit the body (capped) so the open
// edge is human-verifiable at the call site. Image base 0x180000000. Read-only.
//@category KCD2

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class ModelOpenSiteDiscover extends GhidraScript {

    // Model file-ext + loader-class anchors (substring match against defined strings).
    private static final String[] ANCHORS = {
        ".cgf", ".cdf", ".chr", ".skin", ".cga", ".caf",
        "CryCGFLoader", "CGFLoader", "ChunkFile", "CChunkFile", "CStatObj", "StatObj",
        "CharacterManager", "CryCHRLoader", "Cry3DEngine", "I3DEngine", "CGF.cpp",
        // stream/mmap presence anchors (so the bypass hypothesis is checkable in the body):
        "StreamEngine", "StreamAsyncFileRequest", "ReadStream", "StartRead",
        "CreateFileMapping", "MapViewOfFile"
    };
    private static String matchedAnchor(String s) {
        for (String a : ANCHORS) if (s.contains(a)) return a;
        return null;
    }

    // Verified open/read/stream targets. If a loader body NAMES one, that lane is READ.
    private static final String[][] TARGETS = {
        {"FUN_1804605bc", "CCryFile::Open(0x4605BC) -> FOpen-loose-lane"},
        {"FUN_1804614a0", "ICryPak::FOpen(slot36,0x4614A0) -> loose-lane"},
        {"FUN_182418de4", "FOpenRaw(slot35,0x2418DE4)"},
        {"FUN_180461304", "FOpen-by-pak-index/FReopen(slot38,0x461304)"},
        {"FUN_180460b08", "CCryFile::ReadRaw(0x460B08) -> slot38/OS fread"},
        {"FUN_18051cd00", "FGetCachedFileData(slot40,0x51CD00)"},
        {"FUN_18051e1f8", "FReadRaw(slot39,0x51E1F8)"},
        {"FUN_182418b48", "GetFileSize-by-name(slot45,0x2418B48) -> SIZE GATE"},
        {"FUN_180463ec4", "IsFileExist-3arg(slot67,0x463EC4) -> EXIST GATE"},
        {"FUN_18241abcc", "IsFileExist-2arg(slot70,0x241ABCC) -> EXIST GATE"},
        {"FUN_1804647fc", "StreamAsyncFileRequest(0x4647FC) -> MOUNT/STREAM lane"},
        {"FUN_180464b88", "ZipDir::ReadFileStreaming(0x464B88) -> MOUNT/STREAM read leaf"},
        {"FUN_1804898d0", "StreamEngine.cpp(0x4898D0) -> stream"},
        {"FUN_1804d5580", "archive-factory-slot72-MOUNT(0x4D5580)"},
        {"FUN_1804d6910", "ZipDir-uncached-opener-CreateFileA(0x4D6910) -> MOUNT handle mint"},
        {"FUN_1809b2b28", "CryPak _wfopen producer(0x9B2B28) -> buffered FILE*"},
    };

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Map<Function, Set<String>> loaderFns = new LinkedHashMap<>();
        List<String> anchorHits = new ArrayList<>();
        DataIterator strs = currentProgram.getListing().getDefinedData(true);
        for (Data d : strs) {
            if (monitor.isCancelled()) break;
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            String anchor = matchedAnchor(s);
            if (anchor == null) continue;
            Address strAddr = d.getAddress();
            List<String> refFns = new ArrayList<>();
            for (Reference r : rm.getReferencesTo(strAddr)) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) {
                    loaderFns.computeIfAbsent(f, k -> new TreeSet<>()).add(anchor);
                    refFns.add(f.getName() + "@" + f.getEntryPoint() + " (xref@" + r.getFromAddress() + ")");
                }
            }
            String disp = s.length() > 60 ? s.substring(0, 60) + "…" : s;
            anchorHits.add("  @" + strAddr + "  [" + anchor + "]  \"" + disp + "\"  refd-by="
                + (refFns.isEmpty() ? "(none)" : String.join("; ", refFns)));
        }

        println("================================================================================");
        println("STEP 1 - model/loader/stream anchor strings + referencing functions");
        println("================================================================================");
        for (String h : anchorHits) println(h);
        println("\n=== " + loaderFns.size() + " candidate functions referencing a model/loader/stream anchor ===");

        println("\n================================================================================");
        println("STEP 2 - per-loader decompile + open/read/stream lane detection (READ the edge)");
        println("================================================================================");
        for (Map.Entry<Function, Set<String>> e : loaderFns.entrySet()) {
            if (monitor.isCancelled()) break;
            Function f = e.getKey();
            long fa = f.getEntryPoint().getOffset();
            println("\n--------------------------------------------------------------------------------");
            println("FUNC " + f.getName() + " @ 0x" + Long.toHexString(fa)
                + "  via-anchors=" + e.getValue()
                + "  size=" + f.getBody().getNumAddresses());

            DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
            if (dr == null || !dr.decompileCompleted()) {
                println("  <decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ">");
                continue;
            }
            String c = dr.getDecompiledFunction().getC();

            List<String> lanes = new ArrayList<>();
            for (String[] t : TARGETS) if (c.contains(t[0])) lanes.add(t[1] + " [" + t[0] + "]");
            // vtable-offset call shapes the decompiler emits for slot calls on a CryPak*.
            if (c.contains("+ 0x120)")) lanes.add("vtable+0x120 (FOpen slot36) call-shape");
            if (c.contains("+ 0x130)")) lanes.add("vtable+0x130 (slot38 FRead-family) call-shape");
            if (c.contains("+ 0x118)")) lanes.add("vtable+0x118 (FOpenRaw slot35) call-shape");
            if (c.contains("+ 0x168)")) lanes.add("vtable+0x168 (GetFileSize slot45) call-shape -> SIZE GATE");
            if (c.contains("+ 0x218)")) lanes.add("vtable+0x218 (IsFileExist slot67) call-shape -> EXIST GATE");
            if (c.contains("CreateFileMapping") || c.contains("MapViewOfFile"))
                lanes.add("WIN32 memory-map in body");
            if (c.contains("CreateFileA") || c.contains("CreateFileW"))
                lanes.add("WIN32 CreateFile in body");

            println("  LANES detected in body: " + (lanes.isEmpty()
                ? "(none - body names no known open/read/stream target)" : String.join(" | ", lanes)));

            String[] lines = c.split("\n");
            int limit = Math.min(lines.length, 160);
            for (int i = 0; i < limit; i++) println("    " + lines[i]);
            if (lines.length > limit) println("    … (" + (lines.length - limit) + " more lines)");
        }

        di.dispose();
        println("\ndone.");
    }
}
