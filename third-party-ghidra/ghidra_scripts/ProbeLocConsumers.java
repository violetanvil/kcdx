// ProbeLocConsumers.java -- does the loc int-ID link to consuming functions?
// (parallel-ghidra-research.md §6 gating probe.)
//
// Question: do gameplay functions get localized text by INT-ID (a by-index/by-ID
// getter, int-ID as operand -> statically findable -> the runtime key->ID dump
// can bridge to functions) or by KEY STRING (vtable[23] / by-name -> int-ID
// never appears at call sites -> the dump can't bridge)?
//
// Method: locate the CLocalizedStringsManager vtable (from the RTTI complete-
// object-locator near the type descriptor .?AVCLocalizedStringsManager@@ found
// at 0x184a40e80), enumerate its virtual methods, and for each report:
//   - the slot index + offset,
//   - the decompiled prologue (param shape -> does it take int or char*?),
//   - the number of CALLERS (how heavily used).
// The by-key getter is known to be slot 23 (offset 0xB8). We want to know if a
// by-INDEX/by-ID getter exists and how its caller count compares.
//
// Read-only. Run:
//   analyzeHeadless <proj> KCD2 -process WHGame.dll -postScript ProbeLocConsumers.java -noanalysis -readOnly
//
//@category Research

import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class ProbeLocConsumers extends GhidraScript {

    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // 1. Find the vtable symbol. The constructor (FUN_1809f0ce4) referenced
        //    CLocalizedStringsManager::vftable -- ask the symbol table for it.
        Address vtAddr = null;
        for (ghidra.program.model.symbol.Symbol s :
                currentProgram.getSymbolTable().getAllSymbols(false)) {
            if (monitor.isCancelled()) break;
            String nm = s.getName();
            if (nm.contains("CLocalizedStringsManager") && nm.contains("vftable")) {
                vtAddr = s.getAddress();
                println("vftable symbol: " + s.getName(true) + " @ " + vtAddr);
                break;
            }
        }
        if (vtAddr == null) {
            println("CLocalizedStringsManager::vftable symbol NOT found -- "
                    + "fallback: inspect the ctor's vtable store manually.");
            di.dispose();
            return;
        }

        // 2. Walk the vtable: each 8-byte slot is a function pointer until we hit
        //    a non-code pointer. Report each method: slot, target, callers, prologue.
        println("\n=== CLocalizedStringsManager vtable methods ===");
        for (int slot = 0; slot < 64; slot++) {
            if (monitor.isCancelled()) break;
            Address slotAddr = vtAddr.add(slot * 8L);
            long target;
            try {
                target = mem.getLong(slotAddr) & 0xFFFFFFFFFFFFFFFFL;
            } catch (Exception e) {
                break;
            }
            if (target < base || target > base + 0x10000000L) break; // end of vtable
            Address fnAddr = toAddr(target);
            Function fn = getFunctionAt(fnAddr);
            if (fn == null) fn = getFunctionContaining(fnAddr);
            if (fn == null) {
                println(String.format("slot %2d (off 0x%X): %s  <no function>",
                        slot, slot * 8, fnAddr));
                continue;
            }

            // caller count
            int callers = 0;
            for (Reference r : getReferencesTo(fn.getEntryPoint())) {
                if (r.getReferenceType().isCall() || r.getReferenceType().isData())
                    callers++;
            }

            // prologue: first line of decompile shows the param shape (int vs char*)
            String sig = "?";
            DecompileResults dr = di.decompileFunction(fn, 20, new ConsoleTaskMonitor());
            if (dr != null && dr.decompileCompleted()
                    && dr.getDecompiledFunction() != null) {
                sig = dr.getDecompiledFunction().getSignature();
                if (sig != null) sig = sig.replaceAll("\\s+", " ").trim();
            }
            println(String.format("slot %2d (off 0x%X): %s size=%d xrefs=%d",
                    slot, slot * 8, fn.getName(), fn.getBody().getNumAddresses(),
                    callers));
            println("            sig: " + sig);
        }

        di.dispose();
        println("\n=== interpretation ===");
        println("Look for: a getter taking an INT/uint param (by-index/by-ID -> "
                + "int-ID links to callers) vs only char*/key-string getters "
                + "(by-name -> int-ID does NOT appear at call sites).");
        println("Slot 23 (off 0xB8) is the known by-KEY lookup from LocalizeString.");
        println("=== done ===");
    }
}
