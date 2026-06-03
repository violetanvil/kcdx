// Verify ICVar::GetIVal's vtable slot in KCD2's ICVar vtable (AP3 — not the
// canonical slot on faith). Lead: _pakpriority_reg_helper.txt:69/75 shows the
// ICVar* (from GetCVar) get/set int pair — [0x10]()->int captured, [0x38](int)
// restored. This reads the BODIES to confirm [0x10] (slot 2) IS GetIVal (returns
// the CVar's stored int), not GetI64Val/GetType.
//   1. FUN_180d1b7d8 — the "pure GetIVal reader" (file-search mode); read its
//      ICVar*->[0x10]() callsite + how it uses the result (as an int).
//   2. FUN_180d1b7d8's body to find the actual GetIVal function it calls at +0x10,
//      then decompile THAT function (the accessor body — does it return the int
//      value field off the ICVar?).
// We resolve the slot-2 target by reading the indirect call's resolved target if
// Ghidra has it, else we report the callsite + the get/set-pair evidence.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpICVarGetIVal extends GhidraScript {

    void dumpFunc(long fa, String label, DecompInterface di, AddressSpace sp, Listing listing,
                  boolean withDisasm) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================================");
        println(label + " @ " + a);
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION at this address"); return; }
        println("  proto: " + f.getPrototypeString(true, false) + "  size=" + f.getBody().getNumAddresses());
        println("  --- decompiled C ---");
        DecompileResults dr = di.decompileFunction(f, 90, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            println(dr.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ")");
        }
        if (withDisasm) {
            println("  --- disasm (find the [rax+0x10] indirect call -> the GetIVal slot target) ---");
            Instruction ins = listing.getInstructionAt(f.getEntryPoint());
            int n = 0;
            while (ins != null && f.getBody().contains(ins.getAddress()) && n < 400) {
                String s = ins.toString();
                if (s.contains("CALL") || s.contains("0x10]") || s.contains("0x38]")) {
                    println(String.format("    %s  %s", ins.getAddress(), s));
                }
                ins = listing.getInstructionAfter(ins.getAddress());
                n++;
            }
        }
    }

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing listing = currentProgram.getListing();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // The "pure GetIVal reader" the dump named (file-search mode CVar read).
        // Read its body: it GetCVar(name) then [0x10]() and uses the result as an int.
        dumpFunc(0x180d1b7d8L, "FUN_180d1b7d8 (pure GetIVal reader — file-search mode)", di, sp, listing, true);

        // The reg-helper that captures GetIVal [0x10] + restores via SetIVal [0x38]
        // (re-read its body for the get/set-int pair, the slot-2 corroboration).
        dumpFunc(0x180e384d8L, "FUN_180e384d8 (REGISTER_CVAR2-by-ref helper — [0x10] get / [0x38] set pair)", di, sp, listing, true);

        println("\ndone.");
    }
}
