// Decompile the pak-CVar register wrapper FUN_180e384d8 to learn its real ABI:
//   which arg (if any) is the VF_* flags bitmask, and the default-value source.
// Also: dump the static initializer of the value slot DAT_184927298 (the default),
//   and scan the whole binary for any ->Set / store-to-DAT_184927298 AFTER cfg load
//   that would pin the value to 2 ("pins it" mechanism).
//@category KCD2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class PakPriorityRegHelper extends GhidraScript {
    AddressSpace sp; Memory mem; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    void decompFull(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DECOMP " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no function)"); return; }
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 90, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed)");
    }
    void disasmFn(long va, String label) {
        Function f = getFunctionAt(a(va));
        if (f == null) f = getFunctionContaining(a(va));
        println("\n===== DISASM " + label + " @ 0x" + Long.toHexString(va) + " =====");
        if (f == null) { println("  (no fn)"); return; }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        int n=0;
        while (it.hasNext()) {
            Instruction ins = it.next();
            Address[] fl = ins.getFlows();
            StringBuilder ann = new StringBuilder();
            if (fl!=null) for (Address t: fl){ Function tf=getFunctionContaining(t);
                ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                if(tf!=null) ann.append(" ("+tf.getName()+")"); }
            println("  0x"+Long.toHexString(ins.getAddress().getOffset())+": "+ins.toString()+ann);
            if (++n>400){ println("  ...[trunc]"); break; }
        }
    }
    @Override public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface(); di.openProgram(currentProgram);

        long SLOT = 0x184927298L; // value slot DAT_184927298
        println("=== value slot DAT_184927298 static contents ===");
        try { println("  current bytes (int) = 0x"+Integer.toHexString(mem.getInt(a(SLOT)))+" ("+mem.getInt(a(SLOT))+")"); }
        catch(Exception e){ println("  <unreadable> (likely .bss/uninitialized = 0)"); }

        // The wrapper ABI.
        decompFull(0x180e384d8L, "pak-CVar register wrapper FUN_180e384d8 (ABI + flags)");
        disasmFn(0x180e384d8L, "pak-CVar register wrapper FUN_180e384d8");

        // Compare: the OTHER wrapper used right above with explicit (default,flags) args.
        decompFull(0x180b9bc0cL, "FUN_180b9bc0c (4th-arg-help register variant)");

        // All references to the value slot — find any WRITE (->Set / store) after registration.
        println("\n===== ALL REFERENCES to value slot DAT_184927298 (0x"+Long.toHexString(SLOT)+") =====");
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a(SLOT));
        int n=0;
        while (ri.hasNext()) {
            Reference ref = ri.next(); Address from = ref.getFromAddress();
            Function f = getFunctionContaining(from);
            Instruction ins = getInstructionAt(from);
            println("  ["+(++n)+"] from 0x"+Long.toHexString(from.getOffset())
                +"  type="+ref.getReferenceType()
                +(f!=null? "  fn="+f.getName(): "  (no fn)")
                +(ins!=null? "  insn="+ins.toString(): ""));
        }
        if (n==0) println("  (none)");
        println("\ndone.");
    }
}
