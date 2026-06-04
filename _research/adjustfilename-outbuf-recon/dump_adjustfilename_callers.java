// Verify the caller-side outBuf buffer size that CCryPak::AdjustFileName's by-name
// consumers pass (arg3). FOpen (tier-2 capture _fopen_handle_decomp.txt:190) passes
// a 2048-byte stack buffer (local_858 [2048]) into *(vtable+0x8). This confirms
// consistency across MORE callers before kcdx's HOOK-1 write trusts 2048.
//
// AdjustFileName is a VTABLE method (CCryPak slot 1, +0x8) — callers dispatch via
// *(*this+8), so there is no direct CALL to its RVA to xref. Instead: the concrete
// AdjustFileName body is at RVA 0x6205C; we decompile a set of KNOWN by-name
// consumers (FOpen 0x4614A0 + the existence/size/open vtable-consumer bodies the
// 5-front research named) and, for each, find the `(**(code**)(*this+8))(this,
// name, <ARG3>, flags)` call and report what <ARG3> is declared as (a stack buffer
// of size N, or a forwarded pointer). The question: is arg3 a >=2048 buffer across
// callers?
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpAdjustFileNameCallers extends GhidraScript {

    AddressSpace sp;
    DecompInterface di;

    void dump(long fa, String label) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================");
        println(label + " @ " + a);
        println("================================================================");
        if (f == null) { println("  NO FUNCTION"); return; }
        DecompileResults dr = di.decompileFunction(f, 120, new ConsoleTaskMonitor());
        if (dr == null || !dr.decompileCompleted()) { println("  (decompile failed)"); return; }
        String c = dr.getDecompiledFunction().getC();
        // Print only: local buffer declarations (the [N] arrays) + any line calling
        // *(*x + 8)( ... ) — the AdjustFileName vtable dispatch — so the reviewer
        // sees what buffer is passed as arg3. Keeps output bounded (no whole-body dump).
        String[] lines = c.split("\n");
        println("  --- local buffer decls (candidate outBuf) ---");
        for (String ln : lines) {
            String t = ln.trim();
            if (t.matches(".*\\b(undefined1?|char|byte)\\s+[a-zA-Z_0-9]+\\s*\\[[0-9a-fx]+\\];")) {
                println("    " + t);
            }
        }
        println("  --- vtable+8 (AdjustFileName) dispatch sites + the 3rd arg ---");
        for (String ln : lines) {
            if (ln.contains("+ 8))") || ln.contains("+ 8U))") || ln.contains("*pCryPak + 8")) {
                println("    " + ln.trim());
            }
        }
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // The real AdjustFileName callers, RVAs from the front-1 vtable-surface recon
        // (front1-full-vtable-surface.md). Each row's notes confirm it calls slot 1.
        // FOpen — the dominant caller (re-confirm tier-2: arg3 = local_858 [2048]).
        dump(0x1804614A0L, "slot 36 FOpen (id 131) — AdjustFileName caller [feats=PATHCAP2048]");
        // slot 35 FOpenRaw — "open-into-caller-buffer", AdjustFileName(slot1, flag 0).
        dump(0x182418de4L, "slot 35 FOpenRaw / open-into-caller-buffer — AdjustFileName caller");
        // slot 45 GetFileSize-by-name (this,name,bDiskOnly) — AdjustFileName then OS size.
        dump(0x182418b48L, "slot 45 GetFileSize-by-name — AdjustFileName caller");

        println("\ndone.");
    }
}
