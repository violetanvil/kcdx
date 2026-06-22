// KI-0028 — locate the CShaderMan / .cfxb-cache-consumer call sites by string anchor.
// Discovery model (ENUMERATION-FINDINGS): string literal -> reference site -> caller-graph walk.
// We scan defined strings for shader-system anchors, then for each hit list the functions that
// reference it (the LEA xref sites) — those are the entry points the .cfxb->PSO consumer lives in/near.
// Read-only. Emits ANCHOR / XREF lines for the fronts to body-read.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.data.StringDataInstance;

public class FindShaderManAnchors extends GhidraScript {

    // The anchors we care about, lowercased for case-insensitive substring match.
    // Cover: the cache file extensions, the shader-system class/method names CryEngine uses,
    // the cache index files, and the build-path .cpp markers that pin a function to the shader code.
    static final String[] NEEDLES = {
        ".cfxb", ".cfib", ".cfx", ".cfi",
        "cshaderman", "shaderman", "mfinitshaders", "mfloadshader", "mfpreload",
        "chwshader", "shadercache", "shader.cpp", "shadercache.cpp", "shaderpipeline",
        "lookupdata.bin", "globals.txt", "gameshaders",
        "createpipelinestate", "pipelinestate", "cfxb", "fxshader",
        "mfcompile", "compileshader", "mfcreatepipeline", "psocache", "pso cache"
    };

    boolean matches(String s) {
        String l = s.toLowerCase();
        for (String n : NEEDLES) if (l.contains(n)) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        int hits = 0;
        println("=== SHADER-SYSTEM STRING ANCHORS + xref functions ===");
        while (it.hasNext() && !monitor.isCancelled()) {
            Data d = it.next();
            if (d == null) continue;
            String type = d.getDataType().getName().toLowerCase();
            if (!type.contains("string") && !type.contains("char")) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            if (s.length() < 4 || s.length() > 120) continue;
            if (!matches(s)) continue;

            Address sa = d.getAddress();
            ReferenceIterator refs = rm.getReferencesTo(sa);
            // only report strings that are actually referenced (a real anchor)
            StringBuilder sb = new StringBuilder();
            int n = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                String fn = (f != null) ? f.getName() : "<no-fn>";
                Address fe = (f != null) ? f.getEntryPoint() : from;
                sb.append("    XREF from=0x").append(from).append(" fn=").append(fn)
                  .append(" entry=0x").append(fe).append("\n");
                n++;
                if (n >= 12) { sb.append("    ... (more)\n"); break; }
            }
            if (n == 0) continue; // unreferenced literal, skip
            hits++;
            println("ANCHOR 0x" + sa + " \"" + s.replace("\n","\\n") + "\"  (" + n + " xref)");
            print(sb.toString());
        }
        println("\n=== " + hits + " referenced shader-system anchors ===");
        println("done.");
    }
}
