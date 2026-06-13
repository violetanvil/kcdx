// DumpScriptTableCtor.java -- read the CScriptTable constructor FUN_18071ed18
// (called by CreateTable) to find the vtable it installs at [obj+0], then dump
// that vtable's slot +0xb0 (the CallFunction dispatcher the entity-script fire
// sites invoke) and decompile its target. READ bodies; never infer.
//@category Research

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpScriptTableCtor extends GhidraScript {
    private final long IB = 0x180000000L;

    private void decompVA(DecompInterface di, long va, String why) {
        Function f = getFunctionContaining(toAddr(va));
        if (f == null) { println("\n[" + why + "] no fn at 0x" + Long.toHexString(va)); return; }
        println("\n" + "-".repeat(72));
        println("DECOMP " + why + ": " + f.getName() + " @ " + f.getEntryPoint()
                + " (RVA 0x" + Long.toHexString(f.getEntryPoint().getOffset() - IB)
                + ")  size=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            String[] ls = dr.getDecompiledFunction().getC().split("\n");
            int lim = Math.min(ls.length, 120);
            for (int i = 0; i < lim; i++) println("  " + ls[i]);
            if (ls.length > lim) println("  … (" + (ls.length - lim) + " more)");
        } else println("  <decompile failed>");
    }

    private void dumpVtable(long vtblVA, int n, String label) throws Exception {
        println("\n=== vtable " + label + " @ 0x" + Long.toHexString(vtblVA)
                + " (RVA 0x" + Long.toHexString(vtblVA - IB) + ") ===");
        for (int i = 0; i < n; i++) {
            long t = currentProgram.getMemory().getLong(toAddr(vtblVA + (long) i * 8));
            if (t < IB || t > IB + 0x10000000L) { println(String.format("  slot[%2d] off=0x%-3x -> 0x%x (not code)", i, i * 8, t)); continue; }
            Function f = getFunctionAt(toAddr(t));
            println(String.format("  slot[%2d] off=0x%-3x -> 0x%x (RVA 0x%x) %s",
                    i, i * 8, t, t - IB, f != null ? f.getName() : "(no fn defined)"));
        }
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long ctor = 0x18071ed18L;
        decompVA(di, ctor, "CScriptTable ctor FUN_18071ed18 (alloc+vtable install)");

        // Scan the ctor body for `lea reg,[rip+x]` feeding a `mov [reg+0], rax`
        // vtable store. Simpler: collect every lea-rip target that points into
        // .rdata and looks like a vtable (slot0 is code). Report candidates +
        // dump slot +0xb0 of each.
        println("\n=== lea-rip .rdata targets in ctor (vtable candidates) ===");
        Function f = getFunctionContaining(toAddr(ctor));
        java.util.LinkedHashSet<Long> cands = new java.util.LinkedHashSet<>();
        if (f != null) {
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            for (Instruction ins : it) {
                if (!ins.getMnemonicString().equals("LEA")) continue;
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Address) {
                            long ta = ((Address) o).getOffset();
                            // a vtable: slot0 within image, points to code
                            if (ta > IB && ta < IB + 0x6000000L) {
                                try {
                                    long s0 = currentProgram.getMemory().getLong(toAddr(ta));
                                    if (s0 > IB && s0 < IB + 0x6000000L) cands.add(ta);
                                } catch (Exception e) {}
                            }
                        }
                    }
                }
            }
        }
        for (long c : cands) {
            println("  candidate vtable @ 0x" + Long.toHexString(c) + " (RVA 0x" + Long.toHexString(c - IB) + ")");
        }
        // Dump slot +0xb0 region for each candidate.
        for (long c : cands) {
            dumpVtable(c, 26, "candidate");
            long slotB0 = currentProgram.getMemory().getLong(toAddr(c + 0xb0));
            if (slotB0 > IB && slotB0 < IB + 0x6000000L) {
                decompVA(di, slotB0, "slot +0xb0 target of vtable@0x" + Long.toHexString(c));
            }
        }

        di.dispose();
        println("\n=== done ===");
    }
}
