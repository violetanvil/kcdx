// PROBE U.5b — pin arg5 role + the failure branch of the OpenPack register worker.
// Decompile FUN_1804d495c (the real open-and-register, takes param_5 = OpenPack arg5),
// FUN_1804d506c (the predicate), FUN_1804d9a2c (the local_38 init), and the plain
// adjuster FUN_1804621bc. Confirms what arg5 is and what makes the worker return 0.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class PakOpenPackU5b extends GhidraScript {
    DecompInterface dec;
    void dump(long va, String label) throws Exception {
        Address a = toAddr(va);
        Function f = getFunctionContaining(a);
        println("\n========== " + label + " @ 0x" + Long.toHexString(va) + " ==========");
        if (f == null) { println("(no function)"); return; }
        DecompileResults r = dec.decompileFunction(f, 90, monitor);
        if (r == null || !r.decompileCompleted()) { println("DECOMP FAILED"); return; }
        println(r.getDecompiledFunction().getC());
    }
    @Override public void run() throws Exception {
        dec = new DecompInterface();
        dec.openProgram(currentProgram);
        dump(0x1804d495cL, "U5b open+register FUN_1804d495c (consumes arg5)");
        dump(0x1804d506cL, "U5b predicate FUN_1804d506c");
        dump(0x1804d9a2cL, "U5b local_38 init FUN_1804d9a2c");
        dump(0x1804621bcL, "U5b plain AdjustFileName core FUN_1804621bc");
        println("\ndone.");
    }
}
