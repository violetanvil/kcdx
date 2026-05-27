// Dump return-type evidence for IConsole::RemoveCommand (RVA 0x0100955C, vtable[34])
// and IConsole::ExecuteString (RVA 0x007A5818, vtable[35]).
// For each: Ghidra-inferred signature, decompiled C body, full disassembly (to read the
// return path -- is eax/rax set before ret, or void?), and callers (do any test al/eax?).
// Companion to _research/phase7-recon for cap-21/cap-22 Address Library signature population.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpConsoleReturnTypes extends GhidraScript {

    String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < b.length; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    void dumpFunc(long fa, String label, DecompInterface di, AddressSpace sp, Listing listing) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================================");
        println(label + " @ " + a);
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION at this address"); return; }

        println("  Ghidra inferred prototype: " + f.getPrototypeString(true, false));
        println("  Return type (Ghidra): " + f.getReturnType());
        println("  Calling convention: " + f.getCallingConventionName());
        println("  Function size (addrs): " + f.getBody().getNumAddresses());

        // Decompiled body
        println("  --- decompiled C ---");
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            println(dr.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ")");
        }

        // Full disassembly -- we want every instruction so we can read each ret and what
        // touches eax/rax/al just before it.
        println("  --- full disassembly ---");
        Instruction ins = listing.getInstructionAt(f.getEntryPoint());
        int n = 0;
        while (ins != null && f.getBody().contains(ins.getAddress()) && n < 1200) {
            byte[] ib = ins.getBytes();
            println(String.format("    %s  %-26s  %s", ins.getAddress(), hex(ib), ins));
            ins = listing.getInstructionAfter(ins.getAddress());
            n++;
        }

        // Callers: for return-type corroboration we want to see whether any caller tests the
        // result in al/eax right after the call. We print the caller site address; the caller
        // body inspection is manual from these anchors.
        Reference[] refs = getReferencesTo(f.getEntryPoint());
        println("  --- references to this function: " + refs.length + " ---");
        int shown = 0;
        for (Reference r : refs) {
            if (shown++ >= 20) break;
            Address fromA = r.getFromAddress();
            Function caller = getFunctionContaining(fromA);
            println("    <- " + fromA + "  in " + (caller != null ? caller.getName() + "@" + caller.getEntryPoint() : "<no func>") + "  type=" + r.getReferenceType());
        }
    }

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing listing = currentProgram.getListing();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // RVA 0x0100955C, RVA 0x007A5818 ; image base 0x180000000
        dumpFunc(0x18100955CL, "IConsole::RemoveCommand (id 2001, vtable[34], RVA 0x0100955C)", di, sp, listing);
        dumpFunc(0x1807A5818L, "IConsole::ExecuteString (id 2002, vtable[35], RVA 0x007A5818)", di, sp, listing);

        // ExecuteString dispatches into a helper at 0x007A586C per DISPATCH-INVESTIGATION.md.
        // Dump it too: the thin entry-point's return type often just forwards the helper's.
        dumpFunc(0x1807A586CL, "ExecuteString helper (RVA 0x007A586C, per DISPATCH-INVESTIGATION.md)", di, sp, listing);

        println("\ndone.");
    }
}
