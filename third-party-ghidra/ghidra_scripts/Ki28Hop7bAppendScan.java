// KI-0028 HOP 7b — find the render-item ENQUEUE (append into [passctx+0x298..0x2a0]).
// The item vector is a {begin@+0x298, end@+0x2a0, cap@+0x2a8} on the pass ctx (obj+0x70).
// HOP 6: swap-ON this vector is empty every frame => nobody appends. The dispatcher's
// caller (FUN_1804e8d88, 70 bytes) is a thin wrapper — the fill happens elsewhere, earlier.
// This scan finds functions that WRITE the vector-end field via the push-back idiom:
// an instruction referencing displacement +0x2a0 (MOV [reg+0x2a0], ...) near a +0x298
// reference in the same function. Emits candidate functions to decompile next. Read-only.
//@category KCD2

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

public class Ki28Hop7bAppendScan extends GhidraScript {
    static final String OUTDIR =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-differential-trace-recon\\";

    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(new FileWriter(OUTDIR + "_hop7b_append_scan.txt"));
        Listing lst = currentProgram.getListing();
        // Scope to the render module cluster where pass A + the dispatcher live
        // (0x4e0000..0x900000 covers FUN_1804ec3a0 / 0x501cb0 / 0x779534 / 0x867990).
        long lo = 0x1804e0000L, hi = 0x180900000L;
        // Count, per function, how many instructions touch +0x298 and +0x2a0 displacements.
        Map<Function, int[]> hits = new HashMap<>();  // [c298, c2a0, writes2a0]
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        int scanned = 0;
        while (fit.hasNext()) {
            Function f = fit.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep < lo || ep >= hi) continue;
            scanned++;
            Instruction ins = lst.getInstructionAt(f.getEntryPoint());
            Address max = f.getBody().getMaxAddress();
            int c298 = 0, c2a0 = 0, w2a0 = 0;
            while (ins != null && ins.getAddress().compareTo(max) <= 0) {
                String s = ins.toString();
                boolean has298 = s.contains("0x298");
                boolean has2a0 = s.contains("0x2a0");
                if (has298) c298++;
                if (has2a0) {
                    c2a0++;
                    // a write form: "MOV [reg + 0x2a0], reg" -> "0x2a0]," before a src reg
                    String low = s.toLowerCase();
                    if (low.startsWith("mov") && low.indexOf("0x2a0],") >= 0) w2a0++;
                }
                ins = ins.getNext();
            }
            if (c298 > 0 && c2a0 > 0) hits.put(f, new int[]{c298, c2a0, w2a0});
        }
        out.println("HOP 7b append-scan — functions touching BOTH +0x298 and +0x2a0 in "
            + "0x4e0000..0x900000 (scanned " + scanned + " fns). A push-back into the pass "
            + "item-vector writes +0x2a0 (end ptr) and reads +0x298 (begin). w2a0>0 = a "
            + "vector-end WRITE (append/clear candidate).");
        out.println();
        // Sort by writes-to-2a0 desc, then total refs.
        hits.entrySet().stream()
            .sorted((a, b) -> {
                int wa = a.getValue()[2], wb = b.getValue()[2];
                if (wa != wb) return wb - wa;
                return (b.getValue()[0] + b.getValue()[1]) - (a.getValue()[0] + a.getValue()[1]);
            })
            .limit(40)
            .forEach(e -> {
                int[] v = e.getValue();
                out.println(String.format("  %-14s ref298=%-3d ref2a0=%-3d write2a0=%-2d  %s",
                    "0x" + Long.toHexString(e.getKey().getEntryPoint().getOffset()),
                    v[0], v[1], v[2], e.getKey().getName()));
            });
        out.close();
        println("Ki28Hop7bAppendScan: wrote _hop7b_append_scan.txt (" + hits.size() + " candidates)");
    }
}
