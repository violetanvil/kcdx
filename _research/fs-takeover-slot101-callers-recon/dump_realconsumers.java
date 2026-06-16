// slot-101 recon, pass 6 — tighten the consumer set to GENUINE ICryPak FindFirst callers.
//
// pass 4 found 382 indirect +0x328 call sites, but pass 5 showed FUN_180423b18's +0x328 is
// on a DIFFERENT object's vtable — the raw offset over-counts (AP19: the offset is not the
// object). A genuine CCryPak::FindFirst consumer calls +0x328 on a `this` that is ALSO used
// for other CCryPak slots in the same fn, OR is the pCryPak global. This pass scores each
// +0x328-calling fn by whether it ALSO calls known CryPak-only slots (+0x70 ForEachFile,
// +0x100 FindPakByCRC, +0x1a8 FSeek, +0x118 FOpenRaw) on the same call pattern — a strong
// "this is a CCryPak" corroborator — and decompiles the top corroborated ones to read what
// they do with the returned iterator (asset enumeration?).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class dump_realconsumers extends GhidraScript {
    AddressSpace sp; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }
    void decompFull(long va, String label) {
        Function f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no fn)"); return; }
        println("  name: " + f.getName() + "  size: " + f.getBody().getNumAddresses());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }
    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface(); di.openProgram(currentProgram);

        // CryPak-distinctive slot offsets that co-occur with +0x328 only on a real CCryPak this.
        String[] crypakOffs = {"0x70", "0x100", "0x118", "0x1a8", "0x238", "0x110", "0x90"};
        println("========== +0x328 consumers ALSO touching CryPak-distinctive slots (CCryPak-this corroborated) ==========");
        FunctionIterator fns = currentProgram.getListing().getFunctions(true);
        java.util.List<Long> strong = new java.util.ArrayList<>();
        int scanned = 0, has328 = 0;
        while (fns.hasNext()) {
            Function f = fns.next();
            boolean c328 = false; int crypakHits = 0;
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            java.util.Set<String> offsSeen = new java.util.HashSet<>();
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!ins.getMnemonicString().toLowerCase().startsWith("call")) continue;
                String s = ins.toString();
                if (s.contains("0x328")) c328 = true;
                for (String off : crypakOffs) if (s.contains("[" ) && s.contains("+ " + off + "]")) { offsSeen.add(off); }
            }
            scanned++;
            if (c328) {
                has328++;
                crypakHits = offsSeen.size();
                if (crypakHits >= 2) { strong.add(f.getEntryPoint().getOffset());
                    println("  STRONG fn " + f.getName() + " @0x" + Long.toHexString(f.getEntryPoint().getOffset())
                        + "  co-slots=" + offsSeen); }
            }
        }
        println("  scanned fns: " + scanned + "   with +0x328: " + has328 + "   strongly-CryPak (>=2 co-slots): " + strong.size());

        // decompile up to 6 strong ones to read what they do with the iterator.
        int n = 0;
        for (Long c : strong) { if (n++ >= 6) { println("(>6, stopping)"); break; } decompFull(c, "STRONG CryPak FindFirst consumer #" + n); }

        di.dispose();
        println("\n===== dump_realconsumers COMPLETE =====");
    }
}
