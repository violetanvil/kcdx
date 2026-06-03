// Phase 8.5 — sys_pakPriority CVar REGISTRATION: flags + default, and any post-cfg ->Set(2).
//
// Anchors (verified, prior dump _u5_worker2.txt:231/235):
//   "sys_PakPriority"               @ 0x183a93c00  (the CVar name string)
//   "CVar sys_PakPriority value is %d" @ 0x183dcc770 (the value-log format string)
//
// CryEngine CVar registration (IConsole::RegisterInt / REGISTER_CVAR) passes:
//   (name, default value, flags bitmask VF_*, help string).  We want the flags + default.
//
// This script:
//  (1) Find every reference (LEA xref) to the name string @0x183a93c00.
//      For each, name + decompile the containing function (the registrar) and dump a
//      raw disasm window around the xref so the immediates passed to the register call
//      (default value + VF_ flags bitmask) are visible at the instruction level (AP2/AP3 —
//      read the immediate, do not infer).
//  (2) Find every reference to the value-log string @0x183dcc770 — the function that READS
//      and logs the value (the consumption side), to corroborate the value slot.
//  (3) Scan all functions referencing the name string for any ->Set / store-of-2 to the
//      CVar value slot AFTER registration (the "pins it to 2" mechanism), reported found/not.
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

public class PakPriorityCvarReg extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    String cstr(long va) {
        StringBuilder s = new StringBuilder();
        try {
            for (int i = 0; i < 96; i++) {
                byte b = mem.getByte(a(va + i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) s.append("\\x").append(String.format("%02x", b & 0xff));
                else s.append((char)(b & 0xff));
            }
        } catch (Exception e) { return "<unreadable>"; }
        return s.toString();
    }

    void decompFull(Function f, String label) {
        println("\n===== DECOMP " + label + " @ 0x"
            + Long.toHexString(f.getEntryPoint().getOffset()) + " =====");
        println("  name: " + f.getName() + "  conv: " + f.getCallingConventionName()
            + "  params: " + f.getParameterCount());
        println("  signature: " + f.getSignature().getPrototypeString());
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (r!=null? r.getErrorMessage():"null") + ")");
    }

    // raw disasm window: from `before` instructions ahead of `at` through `after` past it,
    // staying inside the containing function body.
    void disasmWindow(Address at, String label) {
        Function f = getFunctionContaining(at);
        println("\n===== DISASM WINDOW " + label + " (xref @ 0x"
            + Long.toHexString(at.getOffset()) + ") =====");
        if (f == null) { println("  (no containing function)"); return; }
        long lo = at.getOffset() - 0x80;
        long hi = at.getOffset() + 0x90;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            long off = ins.getAddress().getOffset();
            if (off < lo || off > hi) continue;
            String mark = (off == at.getOffset()) ? "  <== name-string LEA" : "";
            Address[] fl = ins.getFlows();
            StringBuilder ann = new StringBuilder();
            if (fl != null) for (Address t : fl) {
                Function tf = getFunctionContaining(t);
                ann.append("  -> 0x").append(Long.toHexString(t.getOffset()));
                if (tf != null) ann.append(" (").append(tf.getName()).append(")");
            }
            println("  0x" + Long.toHexString(off) + ": " + ins.toString() + ann + mark);
        }
    }

    void dumpXrefs(long strVa, String what, boolean decompContainer, boolean window) throws Exception {
        println("\n############################################################");
        println("# XREFS to " + what + " @ 0x" + Long.toHexString(strVa));
        println("#   string = \"" + cstr(strVa) + "\"");
        println("############################################################");
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a(strVa));
        int n = 0;
        while (ri.hasNext()) {
            Reference ref = ri.next();
            Address from = ref.getFromAddress();
            Function f = getFunctionContaining(from);
            println("\n[xref " + (++n) + "] from 0x" + Long.toHexString(from.getOffset())
                + "  type=" + ref.getReferenceType()
                + (f != null ? "  in fn=" + f.getName()
                    + " @0x" + Long.toHexString(f.getEntryPoint().getOffset()) : "  (no fn)"));
            if (f != null && window) disasmWindow(from, what);
            if (f != null && decompContainer) decompFull(f, "registrar (contains xref to " + what + ")");
        }
        if (n == 0) println("  (no references found)");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long NAME = 0x183a93c00L;   // "sys_PakPriority"
        long LOGF = 0x183dcc770L;   // "CVar sys_PakPriority value is %d"

        println("sys_pakPriority CVar registration probe");
        println("  NAME string @0x" + Long.toHexString(NAME) + " = \"" + cstr(NAME) + "\"");
        println("  LOGF string @0x" + Long.toHexString(LOGF) + " = \"" + cstr(LOGF) + "\"");

        // (1) The registrar: xref to the name string, decompile + disasm window for the
        //     immediates (default + VF_ flags) passed to the register call.
        dumpXrefs(NAME, "sys_PakPriority NAME", true, true);

        // (2) The reader: xref to the value-log format string (consumption-side corroboration).
        dumpXrefs(LOGF, "value-log format", true, false);

        println("\ndone.");
    }
}
