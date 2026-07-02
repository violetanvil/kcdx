// Find the C_ModelProperty<bool, C_CombatSignalWithNewValueTrait<bool, I_CombatActor*>> SetValue/notify path.
// The combat-state getter is vtable slot[1] (GetValue) on the property object at [combatComponent+0x90]+0xB60.
// This script: locates the property's vtable from its RTTI col-descriptor at VA 0x184AFFB00,
// dumps ALL its vtable slots, decompiles the value-mutating slot(s), and reads callers of the setter
// to confirm a combat-state TRANSITION (false->true / true->false) + the C_CombatSignal change-notify dispatch.
// analyzeHeadless <proj> KCD2 -process WHGame.dll -scriptPath ghidra_scripts -postScript CombatPropSetter.java -noanalysis -readOnly
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.*;

public class CombatPropSetter extends GhidraScript {
    static final long RTTI_TYPEDESC_VA = 0x184AFFB00L; // .?AV?$C_ModelProperty@_N...C_CombatSignalWithNewValueTrait@_NPEAVI_CombatActor...
    long textStart = 0, textEnd = 0;
    Memory mem; Listing listing; AddressSpace sp; DecompInterface di;

    String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < b.length; i++) { if (i>0) sb.append(' '); sb.append(String.format("%02X", b[i]&0xFF)); }
        return sb.toString();
    }
    boolean inText(long va){ return va >= textStart && va < textEnd; }

    void decompile(Function fn, int max, String tag) throws Exception {
        if (fn == null) { println("  ["+tag+"] no function"); return; }
        DecompileResults r = di.decompileFunction(fn, 60, new ConsoleTaskMonitor());
        println("  === ["+tag+"] "+fn.getName()+" @ "+fn.getEntryPoint()+" size="+fn.getBody().getNumAddresses()+" ===");
        if (r != null && r.decompileCompleted()) {
            String code = r.getDecompiledFunction().getC();
            int i=0; for (String ln : code.split("\n")) { println(String.format("    %3d: %s", i++, ln)); if (i>max) { println("    ...(truncated)"); break; } }
        } else println("    decompile failed: " + (r!=null?r.getErrorMessage():"null"));
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        listing = currentProgram.getListing();
        di = new DecompInterface(); di.openProgram(currentProgram);
        for (MemoryBlock blk : mem.getBlocks()) {
            if (blk.isExecute()) { if (textStart==0) textStart=blk.getStart().getOffset(); long e=blk.getEnd().getOffset(); if (e>textEnd) textEnd=e; }
        }
        println(String.format(".text 0x%X - 0x%X", textStart, textEnd));

        // 1. Find the RTTI Complete Object Locator -> vtable for the type descriptor at RTTI_TYPEDESC_VA.
        // MSVC layout: a TypeDescriptor's VA is referenced by a RTTICompleteObjectLocator (field +0x0C, an RVA-from-imagebase
        // in /GR builds OR a direct ref). The COL is pointed at by [vtable-8]. We search .rdata for an 8-byte pointer == RTTI_TYPEDESC_VA
        // to find the COL, then search for a pointer to the COL to find vtable-8 (vtable = that ref + 8).
        println("\n=== 1. Locating vtable via RTTI typedesc 0x"+Long.toHexString(RTTI_TYPEDESC_VA)+" ===");
        List<Address> refsToTypedesc = new ArrayList<>();
        byte[] tdNeedle = new byte[8];
        for (int i=0;i<8;i++) tdNeedle[i] = (byte)((RTTI_TYPEDESC_VA >> (8*i)) & 0xFF);
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address a = mem.findBytes(blk.getStart(), blk.getEnd(), tdNeedle, null, true, monitor);
            while (a != null) { refsToTypedesc.add(a); Address n=a.add(1); if (n.compareTo(blk.getEnd())>0) break; a=mem.findBytes(n, blk.getEnd(), tdNeedle, null, true, monitor); }
        }
        println("  8-byte ptrs == typedesc VA: " + refsToTypedesc.size());
        // The COL contains the typedesc pointer (or RVA). Try both: a direct 8-byte ptr (above) and a 4-byte RVA.
        long imgBase = 0x180000000L;
        int tdRva = (int)(RTTI_TYPEDESC_VA - imgBase);
        byte[] rvaNeedle = new byte[4];
        for (int i=0;i<4;i++) rvaNeedle[i] = (byte)((tdRva >> (8*i)) & 0xFF);
        List<Address> rvaRefs = new ArrayList<>();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address a = mem.findBytes(blk.getStart(), blk.getEnd(), rvaNeedle, null, true, monitor);
            int guard=0;
            while (a != null && guard++ < 5000) { rvaRefs.add(a); Address n=a.add(1); if (n.compareTo(blk.getEnd())>0) break; a=mem.findBytes(n, blk.getEnd(), rvaNeedle, null, true, monitor); }
        }
        println("  4-byte vals == typedesc RVA 0x"+Long.toHexString(tdRva)+": " + rvaRefs.size() + " (COL has it at +0x0C)");

        // Candidate COLs: a 4-byte RVA ref where the containing structure looks like a COL (signature dword at -0x0C).
        // COL layout (x64): +0x00 signature(1), +0x04 offset, +0x08 cdOffset, +0x0C pTypeDescriptor(RVA), +0x10 pClassDescriptor(RVA), +0x14 pSelf(RVA)
        Set<Long> colVAs = new TreeSet<>();
        for (Address r : rvaRefs) {
            long colVA = r.getOffset() - 0x0C;
            try {
                int sig = mem.getInt(sp.getAddress(colVA));
                if (sig == 1) colVAs.add(colVA); // x64 COL signature == 1
            } catch (Exception e) {}
        }
        println("  candidate COLs (sig==1 at +0): " + colVAs.size());

        // For each COL, find an 8-byte pointer to it in .rdata -> that location+8 is the vtable start.
        Set<Long> vtableVAs = new TreeSet<>();
        for (long colVA : colVAs) {
            byte[] colNeedle = new byte[8];
            for (int i=0;i<8;i++) colNeedle[i] = (byte)((colVA >> (8*i)) & 0xFF);
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized() || blk.isExecute()) continue;
                Address a = mem.findBytes(blk.getStart(), blk.getEnd(), colNeedle, null, true, monitor);
                int guard=0;
                while (a != null && guard++ < 50) {
                    long vt = a.getOffset() + 8;
                    // vtable[0] must be in .text
                    try { long s0 = mem.getLong(sp.getAddress(vt)); if (inText(s0)) vtableVAs.add(vt); } catch (Exception e) {}
                    Address n=a.add(1); if (n.compareTo(blk.getEnd())>0) break; a=mem.findBytes(n, blk.getEnd(), colNeedle, null, true, monitor);
                }
            }
        }
        println("  vtable VAs (COL-ptr +8, slot0 in .text): " + vtableVAs.size());
        for (long v : vtableVAs) println("    vtable @ 0x"+Long.toHexString(v));

        // 2. For each vtable, dump up to 12 slots; decompile slot[1] (getter) and any slot that writes a value + dispatches.
        for (long vtVA : vtableVAs) {
            println("\n=== 2. vtable 0x"+Long.toHexString(vtVA)+" slots ===");
            Address vt = sp.getAddress(vtVA);
            List<Long> slots = new ArrayList<>();
            for (int i=0;i<16;i++) {
                long s; try { s = mem.getLong(vt.add(8L*i)); } catch (Exception e){ break; }
                if (!inText(s)) { println(String.format("  slot[%d] = 0x%X (not .text, stop)", i, s)); break; }
                slots.add(s);
                Function f = getFunctionContaining(sp.getAddress(s));
                println(String.format("  slot[%d] = 0x%X  %s", i, s, f!=null?f.getName():"<no fn>"));
            }
            // slot[1] = getter; decompile it + slot[2]/slot[3] (commonly SetValue / notify) and any others
            for (int idx : new int[]{1,2,3,4,5}) {
                if (idx >= slots.size()) continue;
                Function f = getFunctionContaining(sp.getAddress(slots.get(idx)));
                decompile(f, 70, "slot["+idx+"]");
            }
        }

        // 3. The setter is the consumer of interest. Find callers of slot[2]/slot[3] across the binary, and
        // grep for an immediate-arg call shape (SetValue(prop, true/false)).
        println("\n=== 3. setter callers (slot[2..4] of each vtable) ===");
        for (long vtVA : vtableVAs) {
            Address vt = sp.getAddress(vtVA);
            for (int idx : new int[]{2,3,4}) {
                long s; try { s = mem.getLong(vt.add(8L*idx)); } catch (Exception e){ continue; }
                if (!inText(s)) continue;
                Address sa = sp.getAddress(s);
                Function fn = getFunctionContaining(sa);
                Reference[] refs = getReferencesTo(sa);
                println(String.format("  vtable 0x%X slot[%d] -> 0x%X (%s): %d direct refs",
                    vtVA, idx, s, fn!=null?fn.getName():"<no fn>", refs.length));
                int shown=0;
                for (Reference r : refs) {
                    if (shown++ >= 10) break;
                    Function caller = getFunctionContaining(r.getFromAddress());
                    println("      <- "+r.getFromAddress()+" in "+(caller!=null?caller.getName():"<no fn>"));
                }
            }
        }
        println("\ndone.");
    }
}
