// EnumerateFunctions.java -- game-agnostic per-function inventory dump for the
// parallel-ghidra-research orchestration (docs/outstanding-work/parallel-ghidra-research.md).
//
// Emits one CSV per analyzed program: the raw partition-planning inventory the
// orchestrator clusters into disjoint subagent partitions, and (on re-runs /
// other-game ports) the function list the survival-check dump iterates.
//
// Java (not Jython): Ghidra 12.1 removed bundled Jython, so .py GhidraScripts
// abort headless ("install the Jython Extension, or port to PyGhidra or Java").
// Java is the runtime Ghidra ships with -- zero per-machine setup, the most
// portable choice for re-runs on a new game build AND ports to other games.
//
// REUSABLE BY DESIGN -- nothing game-specific is hardcoded:
//   * image base is read from the program (no 0x180000000 constant),
//   * module name comes from the program name (no "WHGame.dll" literal),
//   * auto-name prefixes are the Ghidra-universal set (FUN_/SUB_/LAB_/...),
//   * output dir + an optional version tag arrive as script args.
// Point it at ANY analyzed Ghidra program, in this project or a future game's.
//
// Run (single program, read-only -- never mutates the project):
//   analyzeHeadless <proj_dir> <proj_name> -process <Program> \
//       -postScript EnumerateFunctions.java <out_dir> [version_tag] \
//       -noanalysis -readOnly
// Run across every program already in the project: drop -process.
//
// Args (getScriptArgs()):
//   [0] out_dir       REQUIRED. Directory to write <module>.functions.csv into.
//   [1] version_tag   OPTIONAL. Game-version label stamped into each row's
//                     game_version column. Defaults to "" -- maintainer fills
//                     it at import time.
//
// CSV columns (a SUBSET of parallel-ghidra-research.md's per-function deliverable
// -- only the fields Ghidra emits mechanically with no inference; signature,
// content_hash, decompile_quality, subsystem, inferred_purpose are added by the
// per-partition subagents downstream, NOT here):
//   module, game_version, rva, auto_name, function_name, namespace,
//   is_auto_named, size_bytes, is_thunk, is_external, calling_convention
//
//@category Research

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Namespace;

public class EnumerateFunctions extends GhidraScript {

    // Ghidra's universal auto-name prefixes -- a name beginning with any of
    // these is a Ghidra-synthesized placeholder, NOT a recovered/source
    // identifier. Engine-version-stable and game-independent.
    private static final String[] AUTO_PREFIXES = {
        "FUN_", "SUB_", "LAB_", "thunk_FUN_", "UndefinedFunction_",
        "DAT_", "EXT_", "switchD_", "caseD_", "j_"
    };

    private static boolean isAutoName(String name) {
        for (String p : AUTO_PREFIXES) {
            if (name.startsWith(p)) {
                return true;
            }
        }
        return false;
    }

    // Minimal RFC-4180-ish quoting: wrap + double internal quotes when the cell
    // carries a comma, quote, or newline. C++ templated symbols contain commas,
    // so this is not optional.
    private static String csv(String s) {
        if (s == null) {
            s = "";
        }
        if (s.indexOf(',') >= 0 || s.indexOf('"') >= 0
                || s.indexOf('\n') >= 0 || s.indexOf('\r') >= 0) {
            return '"' + s.replace("\"", "\"\"") + '"';
        }
        return s;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("[EnumerateFunctions] ERROR: out_dir arg required.");
            println("  usage: -postScript EnumerateFunctions.java <out_dir> [version_tag]");
            throw new IllegalArgumentException("out_dir arg required");
        }
        String outDir = args[0];
        String versionTag = (args.length >= 2) ? args[1] : "";

        String module = currentProgram.getName();
        long imageBase = currentProgram.getImageBase().getOffset();

        File dir = new File(outDir);
        if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) {
            // race-tolerant: a sibling -process iteration may have just made it
            throw new java.io.IOException("could not create out_dir: " + outDir);
        }

        File outFile = new File(dir, module + ".functions.csv");
        PrintWriter w = new PrintWriter(outFile, "UTF-8");
        try {
            w.println("module,game_version,rva,auto_name,function_name,namespace,"
                    + "is_auto_named,size_bytes,is_thunk,is_external,calling_convention");

            long total = 0, named = 0, auto = 0, thunks = 0, externals = 0;

            FunctionIterator fns = currentProgram.getListing().getFunctions(true);
            while (fns.hasNext()) {
                if (monitor.isCancelled()) {
                    break;
                }
                Function fn = fns.next();
                total++;

                Address entry = fn.getEntryPoint();
                long rva = entry.getOffset() - imageBase;

                String name = fn.getName();
                boolean autoNamed = isAutoName(name);
                if (autoNamed) {
                    auto++;
                } else {
                    named++;
                }

                // auto_name: ALWAYS populated (brief requirement). When the
                // function carries a real name, synthesize the canonical
                // FUN_<va> placeholder from its entry VA so the column is never
                // empty -- auto-names are per-version-RVA-derived by definition.
                String autoName = autoNamed
                        ? name
                        : String.format("FUN_%08x", entry.getOffset());
                // function_name: the recovered/source name, empty when only an
                // auto-name exists (brief: "Empty/null for uncategorized").
                String functionName = autoNamed ? "" : name;

                Namespace ns = fn.getParentNamespace();
                String namespace = (ns == null || ns.isGlobal())
                        ? "" : ns.getName(true);

                boolean isThunk = fn.isThunk();
                if (isThunk) {
                    thunks++;
                }
                boolean isExternal = fn.isExternal();
                if (isExternal) {
                    externals++;
                }

                long sizeBytes = fn.getBody().getNumAddresses();

                String cc = fn.getCallingConventionName();
                if (cc == null) {
                    cc = "";
                }

                StringBuilder row = new StringBuilder();
                row.append(csv(module)).append(',')
                   .append(csv(versionTag)).append(',')
                   .append(csv("0x" + Long.toHexString(rva))).append(',')
                   .append(csv(autoName)).append(',')
                   .append(csv(functionName)).append(',')
                   .append(csv(namespace)).append(',')
                   .append(autoNamed ? "1" : "0").append(',')
                   .append(sizeBytes).append(',')
                   .append(isThunk ? "1" : "0").append(',')
                   .append(isExternal ? "1" : "0").append(',')
                   .append(csv(cc));
                w.println(row.toString());
            }

            w.flush();

            // Summary to stdout -- the partition-planning signal the orchestrator
            // reads without parsing the CSV. Namespace coverage is the
            // clustering lever.
            String bar = "========================================================================";
            println(bar);
            println(String.format("[EnumerateFunctions] module=%s  base=0x%x  version=%s",
                    module, imageBase, versionTag.isEmpty() ? "(unset)" : versionTag));
            println("  wrote: " + outFile.getAbsolutePath());
            println(String.format("  functions total : %d", total));
            println(String.format("    named (source) : %d", named));
            println(String.format("    auto-named     : %d", auto));
            println(String.format("    thunks         : %d", thunks));
            println(String.format("    externals      : %d", externals));
            println(bar);
        } finally {
            w.close();
        }
    }
}
