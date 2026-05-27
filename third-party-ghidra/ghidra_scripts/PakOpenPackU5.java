// PROBE U.5 — OpenPack ok=0 RE. Decompile the registration helper FUN_1804d4824
// (the slot-6 body's actual register/open worker, where the false return comes from),
// the AdjustFileName slot-1 FUN_18046205C (what path forms it accepts / re-roots),
// and re-decompile slot-6 FUN_180da4e5c with full var view for arg5/arg6 roles.
// Also scan for OpenPack-flags strings + the registration helper's failure-path strings.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;

public class PakOpenPackU5 extends GhidraScript {
    DecompInterface dec;

    void dump(long va, String label) throws Exception {
        Address a = toAddr(va);
        Function f = getFunctionContaining(a);
        println("\n========== " + label + " @ 0x" + Long.toHexString(va) + " ==========");
        if (f == null) { println("(no function at addr)"); return; }
        println("entry=" + f.getEntryPoint() + " name=" + f.getName());
        DecompileResults r = dec.decompileFunction(f, 90, monitor);
        if (r == null || !r.decompileCompleted()) { println("DECOMP FAILED: " + (r==null?"null":r.getErrorMessage())); return; }
        println(r.getDecompiledFunction().getC());
    }

    void xrefsTo(long va, String label) {
        Address a = toAddr(va);
        println("\n--- xrefs TO " + label + " @ 0x" + Long.toHexString(va) + " ---");
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Reference ref : rm.getReferencesTo(a)) {
            Function fc = getFunctionContaining(ref.getFromAddress());
            println("  from " + ref.getFromAddress() + (fc!=null?" in "+fc.getName():"") + " type=" + ref.getReferenceType());
        }
    }

    @Override
    public void run() throws Exception {
        dec = new DecompInterface();
        dec.openProgram(currentProgram);

        // slot-6 OpenPack (re-decompile for arg5/arg6 confirmation)
        dump(0x180da4e5cL, "U5 slot-6 OpenPack FUN_180da4e5c");
        // the registration/open worker — where the false return is produced
        dump(0x1804d4824L, "U5 OpenPack register worker FUN_1804d4824");
        // AdjustFileName slot-1 (vtable+8) — what it does to the path
        dump(0x18046205cL, "U5 AdjustFileName slot-1 FUN_18046205c");
        // who calls OpenCachePak FUN_18243fc40 (to learn what param_3 / param_2 forms are)
        xrefsTo(0x18243fc40L, "OpenCachePak FUN_18243fc40");

        // String scan: OpenPack flags / pak-mount error strings near the worker
        println("\n--- string anchors (pak open / flags / priority) ---");
        ghidra.program.model.listing.DataIterator di = currentProgram.getListing().getDefinedData(true);
        int n = 0;
        while (di.hasNext() && n < 4000000) {
            ghidra.program.model.listing.Data d = di.next();
            n++;
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            String ls = s.toLowerCase();
            if (ls.contains("openpack") || ls.contains("pakpriority") || ls.contains("bindroot")
                || ls.contains("bind root") || ls.contains("not a valid pak") || ls.contains("cdr")
                || ls.contains("central directory") || (ls.contains("pak") && ls.contains("flag"))) {
                println("  @" + d.getAddress() + "  \"" + s + "\"");
            }
        }
        println("\ndone.");
    }
}
