// FRONT 3 (asset-loadpath-map): trace the .cgf/.cdf MODEL load path in WHGame.dll.
// ONE lock-hold pass: (1) find model/character file-ext + loader-class string literals via the
// defined-string listing (xref-reliable), (2) list every function that references them with
// name+RVA+proto, (3) decompile each referencing function AND scan its body for calls to the
// verified read-path slots so the read API it reaches is READ, not inferred:
//     FOpen          slot 36  RVA 0x4614A0  FUN_1804614a0
//     FOpenRaw       slot 35  RVA 0x2418DE4
//     FReopen        slot 38  RVA 0x461304
//     FRead/FGetCachedFileData slot 40 RVA 0x51CD00 FUN_18051cd00
//     CCryFile::Open          RVA 0x4605BC FUN_1804605bc
//     GetFileSize    slot 45  RVA 0x2418B48 ; IsFileExist slot 67 RVA 0x463EC4
// vs a streaming-engine / mmap path (CreateFileMapping / MapViewOfFile / StreamEngine).
// Image base 0x180000000. Read-only. Mirrors FindLocManager.java + DumpFOpenHandleConstruct.java.
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

public class ModelLoadPathDiscover extends GhidraScript {

    // Model/character file-class + loader-class anchors. SUBSTRING match against defined strings.
    private static final String[] ANCHORS = {
        ".cgf", ".cdf", ".chr", ".skin", ".cga", ".caf",
        "CryCGFLoader", "CGFLoader", "ChunkFile", "CStatObj", "StatObj",
        "CharacterManager", "CryCHRLoader", "Cry3DEngine", "I3DEngine",
        // streaming / mmap presence anchors (so the bypass hypothesis is checkable):
        "StreamEngine", "IStreamEngine", "StreamReadBatch", "ReadStream",
        "CreateFileMapping", "MapViewOfFile"
    };

    private static String matchedAnchor(String s) {
        for (String a : ANCHORS) if (s.contains(a)) return a;
        return null;
    }

    // The verified read-path / open targets (RVA -> label). If a loader body calls one of these,
    // we have a READ edge to cite. Detected by scanning the decompiled C for the FUN_ name or the
    // vtable offset, plus an explicit call-target sweep below.
    private static final long[][] READ_TARGETS = {
        {0x1804614a0L, 1}, // FOpen slot36
        {0x182418de4L, 2}, // FOpenRaw slot35
        {0x180461304L, 3}, // FReopen slot38
        {0x18051cd00L, 4}, // FRead/FGetCachedFileData slot40
        {0x1804605bcL, 5}, // CCryFile::Open
        {0x182418b48L, 6}, // GetFileSize slot45
        {0x180463ec4L, 7}, // IsFileExist slot67
        {0x1804618b4L, 8}, // FReadRaw pak-arm leaf
        {0x1804d7ab4L, 9}, // OS-arm CRT fread leaf
    };
    private static String readLabel(int code) {
        switch (code) {
            case 1: return "FOpen(slot36,0x4614A0)";
            case 2: return "FOpenRaw(slot35,0x2418DE4)";
            case 3: return "FReopen(slot38,0x461304)";
            case 4: return "FRead/FGetCachedFileData(slot40,0x51CD00)";
            case 5: return "CCryFile::Open(0x4605BC)";
            case 6: return "GetFileSize(slot45,0x2418B48)";
            case 7: return "IsFileExist(slot67,0x463EC4)";
            case 8: return "FReadRaw-pak-leaf(0x4618B4)";
            case 9: return "OS-arm-fread-leaf(0x4D7AB4)";
            default: return "?";
        }
    }

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Find anchor string literals + the functions referencing them.
        // Keep, per function, the set of anchors that led to it (so we know what class it loads).
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
            String disp = s.length() > 50 ? s.substring(0, 50) + "…" : s;
            anchorHits.add("  @" + strAddr + "  [" + anchor + "]  \"" + disp + "\"  refd-by="
                + (refFns.isEmpty() ? "(none)" : String.join("; ", refFns)));
        }

        println("================================================================================");
        println("STEP 1 — anchor strings + referencing functions");
        println("================================================================================");
        for (String h : anchorHits) println(h);
        println("\n=== " + loaderFns.size() + " candidate functions referencing a model/loader/stream anchor ===");

        // 2. Decompile each candidate; report which verified read-target it calls (READ the edge).
        println("\n================================================================================");
        println("STEP 2 — per-loader decompile + read-edge detection");
        println("================================================================================");
        for (Map.Entry<Function, Set<String>> e : loaderFns.entrySet()) {
            if (monitor.isCancelled()) break;
            Function f = e.getKey();
            long fa = f.getEntryPoint().getOffset();
            println("\n--------------------------------------------------------------------------------");
            println("FUNC " + f.getName() + " @ 0x" + Long.toHexString(fa)
                + "  via-anchors=" + e.getValue()
                + "  proto=" + f.getPrototypeString(true, false)
                + "  size=" + f.getBody().getNumAddresses());

            DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
            if (dr == null || !dr.decompileCompleted()) {
                println("  <decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ">");
                continue;
            }
            String c = dr.getDecompiledFunction().getC();

            // Read-edge detection: which verified read/open target does THIS body call?
            List<String> edges = new ArrayList<>();
            for (long[] t : READ_TARGETS) {
                String funName = "FUN_" + Long.toHexString(t[0]);
                if (c.contains(funName)) edges.add(readLabel((int)t[1]) + " [name " + funName + " in body]");
            }
            // Also scan vtable-offset call shapes the decompiler emits for slot calls on a CryPak*.
            if (c.contains("+ 0x120)")) edges.add("vtable+0x120 (FOpen slot36) call-shape in body");
            if (c.contains("+ 0x140)")) edges.add("vtable+0x140 (FRead slot40) call-shape in body");
            if (c.contains("+ 0x118)")) edges.add("vtable+0x118 (FOpenRaw slot35) call-shape in body");
            if (c.contains("+ 0x130)")) edges.add("vtable+0x130 (FReopen slot38) call-shape in body");
            if (c.contains("CreateFileMapping") || c.contains("MapViewOfFile"))
                edges.add("WIN32 memory-map (CreateFileMapping/MapViewOfFile) in body");
            if (c.contains("CreateFileA") || c.contains("CreateFileW"))
                edges.add("WIN32 CreateFile in body");
            if (c.contains("StreamEngine") || c.contains("ReadStream") || c.contains("StartRead"))
                edges.add("streaming-engine call in body");

            println("  READ-EDGES detected in body: " + (edges.isEmpty() ? "(none — body does not directly call a known read/open target)" : String.join(" | ", edges)));

            // Emit the decompiled body (capped) so the edge is human-verifiable at the call site.
            String[] lines = c.split("\n");
            int limit = Math.min(lines.length, 130);
            for (int i = 0; i < limit; i++) println("    " + lines[i]);
            if (lines.length > limit) println("    … (" + (lines.length - limit) + " more lines)");
        }

        di.dispose();
        println("\ndone.");
    }
}
