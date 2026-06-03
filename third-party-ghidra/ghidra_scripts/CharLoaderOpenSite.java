// FRONT F3: trace the CHARACTER (.cdf/.chr/.skin/.caf) load OPEN site.
// Find every fn that references a .chr/.cdf/.skin/.caf string AND the CryCHRLoader/CharacterManager
// loader-class strings; for each, report which open/read/stream target it NAMES so the lane is READ.
// Also: count callers of CReadOnlyChunkFile::Read (FUN_18051cba0) so we know how broadly the CGF
// FOpen open-site is shared (does the character loader reuse the same chunk reader?).
// Image base 0x180000000. Read-only.
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

public class CharLoaderOpenSite extends GhidraScript {

    // Tighter character-class anchors. The .chr/.skin RTTI stubs from the prior run are size-39
    // type stubs; the real loaders carry CryCHRLoader / SkinLoader / chunk strings.
    private static final String[] ANCHORS = {
        "CryCHRLoader", "CHRLoader", "SkinLoader", "CSkinLoader", "CharacterManager",
        "LoaderCHR", "LoadCHR", "LoadSKIN", "Failed to load character",
        "CharacterInstance", ".chr\"", ".skin\"", ".cdf\"", ".caf\"", " skin file", "chr file"
    };
    private static String matchedAnchor(String s) {
        for (String a : ANCHORS) if (s.contains(a)) return a;
        return null;
    }

    private static final String[][] LANES = {
        {"FUN_18051cba0", "CReadOnlyChunkFile::Read(0x51CBA0) -> FOpen-loose (SHARED CGF reader)"},
        {"FUN_1804605bc", "CCryFile::Open(0x4605BC) FOpen-loose"},
        {"FUN_1804614a0", "ICryPak::FOpen(slot36) loose"},
        {"FUN_182418de4", "FOpenRaw(slot35)"},
        {"FUN_180460b08", "CCryFile::ReadRaw(0x460B08)"},
        {"FUN_18051cd00", "FGetCachedFileData(slot40)"},
        {"FUN_1804647fc", "StreamAsyncFileRequest(0x4647FC) MOUNT/STREAM"},
        {"FUN_180464b88", "ZipDir::ReadFileStreaming(0x464B88) MOUNT/STREAM"},
        {"FUN_1804d6910", "ZipDir uncached CreateFileA(0x4D6910) MOUNT"},
        {"FGetCachedFileData", "FGetCachedFileData(named)"},
        {"CreateFileMapping", "mmap"}, {"MapViewOfFile", "mmap"}, {"CreateFile", "WIN32 CreateFile"},
    };

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // (A) Who calls CReadOnlyChunkFile::Read (FUN_18051cba0)? = which loaders share the CGF open-site.
        Address readFn = toAddr(0x18051cba0L);
        Set<String> readCallers = new TreeSet<>();
        for (Reference r : rm.getReferencesTo(readFn)) {
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) readCallers.add(f.getName() + "@0x" + Long.toHexString(f.getEntryPoint().getOffset()));
        }
        println("=== Direct callers of CReadOnlyChunkFile::Read (FUN_18051cba0): " + readCallers.size());
        for (String s : readCallers) println("   " + s);

        // (B) character-loader anchor strings -> referencing fns.
        Map<Function, Set<String>> fns = new LinkedHashMap<>();
        DataIterator strs = currentProgram.getListing().getDefinedData(true);
        for (Data d : strs) {
            if (!d.hasStringValue()) continue;
            Object v = d.getValue(); if (v == null) continue;
            String s = v.toString(); String a = matchedAnchor(s); if (a == null) continue;
            List<String> refs = new ArrayList<>();
            for (Reference r : rm.getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) { fns.computeIfAbsent(f, k -> new TreeSet<>()).add(a); refs.add(f.getName()); }
            }
            String disp = s.length()>55 ? s.substring(0,55)+"…" : s;
            println("STR @" + d.getAddress() + " [" + a + "] \"" + disp + "\" refd-by=" + (refs.isEmpty()?"(none)":String.join(",",refs)));
        }
        println("\n=== " + fns.size() + " character-loader candidate fns ===");
        for (Map.Entry<Function, Set<String>> e : fns.entrySet()) {
            Function f = e.getKey();
            println("\n--------------------------------------------------------------------------------");
            println("FUNC " + f.getName() + " @ 0x" + Long.toHexString(f.getEntryPoint().getOffset())
                + "  via=" + e.getValue() + "  size=" + f.getBody().getNumAddresses());
            DecompileResults dr = di.decompileFunction(f, 90, monitor);
            if (dr == null || !dr.decompileCompleted()) { println("  <decompile failed>"); continue; }
            String c = dr.getDecompiledFunction().getC();
            StringBuilder lanes = new StringBuilder();
            for (String[] t : LANES) if (c.contains(t[0])) lanes.append(t[1]).append(" | ");
            if (c.contains("+ 0x120)")) lanes.append("vt+0x120(FOpen slot36) | ");
            if (c.contains("+ 0x140)")) lanes.append("vt+0x140(slot40 FRead) | ");
            println("  LANES: " + (lanes.length()==0?"(none named)":lanes.toString()));
            String[] lines = c.split("\n"); int lim = Math.min(lines.length, 100);
            for (int i=0;i<lim;i++) println("    " + lines[i]);
            if (lines.length>lim) println("    … ("+(lines.length-lim)+" more)");
        }
        di.dispose();
        println("\ndone.");
    }
}
