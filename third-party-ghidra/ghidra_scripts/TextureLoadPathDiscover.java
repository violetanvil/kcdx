// FRONT F1 (asset-loadpath-map-recon): discover the TEXTURE (.dds) load path in WHGame.dll.
// THE QUESTION: when the engine loads a .dds, does it go through CCryPak::FOpen(slot 36,
// 0x4614A0) + FRead/FGetCachedFileData(slot 40, 0x51CD00), OR memory-map (CreateFileMapping/
// MapViewOfFile), OR stream async? And is there a size/exist check (slot 45/67) before the read?
//
// Strategy (read-only, AP19 — every claim cited at the call site read in the body):
//   1. Find ".dds" + texture-string literals + the functions that xref them (candidate loaders).
//   2. Enumerate the Win32 memory-mapping APIs (CreateFileMapping*/MapViewOfFile*) in the IAT
//      and who calls them — does any texture-path function memory-map?
//   3. Decompile the slot-83 selector FUN_18241785c (CryFile-vs-CIO open path fork) — what
//      decides the fork, and does either arm memory-map?
//   4. Find DIRECT callers of FGetCachedFileData (slot 40 body 0x51CD00) — who reads via it?
// image base 0x180000000.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class TextureLoadPathDiscover extends GhidraScript {
    AddressSpace sp; Memory mem; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    String fnLabel(Address from) {
        Function cf = getFunctionContaining(from);
        if (cf == null) return "<no-fn> @" + from;
        return cf.getName() + " @0x" + Long.toHexString(cf.getEntryPoint().getOffset());
    }

    // collect distinct functions that reference any address in `targets`
    LinkedHashSet<Long> refFns(Collection<Address> targets) {
        LinkedHashSet<Long> fns = new LinkedHashSet<>();
        for (Address t : targets) {
            for (Reference rf : getReferencesTo(t)) {
                Function cf = getFunctionContaining(rf.getFromAddress());
                if (cf != null) fns.add(cf.getEntryPoint().getOffset());
            }
        }
        return fns;
    }

    void decompPrint(long fva, String label, int timeout) throws Exception {
        Function f = getFunctionAt(a(fva));
        if (f == null) f = getFunctionContaining(a(fva));
        println("\n================================================================================");
        println(label + " @0x" + Long.toHexString(fva));
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION"); return; }
        println("  proto: " + f.getPrototypeString(true,false) + "  size(addrs)=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, timeout, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) println(dr.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (dr!=null?dr.getErrorMessage():"null") + ")");
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // ---- 1. memory-mapping API symbols in the binary (IAT thunks) ----
        println("########## SECTION 1: memory-mapping Win32 APIs + callers ##########");
        String[] mmapApis = {"CreateFileMappingA","CreateFileMappingW","MapViewOfFile",
                             "MapViewOfFileEx","UnmapViewOfFile","OpenFileMappingA","OpenFileMappingW"};
        SymbolIterator allSyms = currentProgram.getSymbolTable().getAllSymbols(true);
        Map<String,List<Address>> mmapSymAddrs = new LinkedHashMap<>();
        for (String n : mmapApis) mmapSymAddrs.put(n, new ArrayList<>());
        while (allSyms.hasNext()) {
            Symbol s = allSyms.next();
            String nm = s.getName();
            for (String n : mmapApis) {
                if (nm.equals(n)) mmapSymAddrs.get(n).add(s.getAddress());
            }
        }
        for (String n : mmapApis) {
            List<Address> addrs = mmapSymAddrs.get(n);
            if (addrs.isEmpty()) { println("  " + n + ": NOT PRESENT in symbol table"); continue; }
            println("  " + n + ": " + addrs.size() + " symbol(s)");
            LinkedHashSet<Long> callers = new LinkedHashSet<>();
            for (Address sa : addrs) {
                println("    sym @" + sa);
                for (Reference rf : getReferencesTo(sa)) {
                    callers.add(0L); // placeholder; print below
                    println("      <- " + fnLabel(rf.getFromAddress()) + "  (refFrom " + rf.getFromAddress() + ")");
                }
            }
        }

        // ---- 2. .dds + texture string literals, and the functions that xref them ----
        println("\n########## SECTION 2: texture/.dds string literals + referencing functions ##########");
        String[] needles = {".dds", ".DDS", "CreateTexture", "Texture::", "CTexture",
                            "TextureStreaming", "StreamReadTexture", "texture", "StreamOnComplete",
                            "DDSHeader", "dds", "$dyntex", "EngineAssets"};
        DataIterator dataIt = currentProgram.getListing().getDefinedData(true);
        Map<String,List<Address>> hitStrings = new LinkedHashMap<>();
        int scanned = 0, strs = 0;
        while (dataIt.hasNext()) {
            Data d = dataIt.next();
            scanned++;
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            strs++;
            String sv = (String) v;
            for (String nd : needles) {
                if (sv.contains(nd)) {
                    hitStrings.computeIfAbsent(nd, k->new ArrayList<>()).add(d.getAddress());
                    break;
                }
            }
        }
        println("  defined-data scanned=" + scanned + " strings=" + strs);
        // print compactly: a few representative hits per needle + their referencing fns
        for (Map.Entry<String,List<Address>> e : hitStrings.entrySet()) {
            List<Address> as = e.getValue();
            println("\n  needle \"" + e.getKey() + "\": " + as.size() + " string(s)");
            int shown = 0;
            for (Address sa : as) {
                Reference[] rs = getReferencesTo(sa);
                if (rs.length == 0) continue;
                Data d = getDataAt(sa);
                String txt = (d!=null && d.getValue() instanceof String) ? (String)d.getValue() : "?";
                println("    @" + sa + " \"" + txt + "\"  ("+rs.length+" xref)");
                for (Reference rf : rs) println("        <- " + fnLabel(rf.getFromAddress()));
                if (++shown >= 8) { println("    ...(" + (as.size()-shown) + " more strings for this needle)"); break; }
            }
        }

        // ---- 3. the slot-83 CryFile-vs-CIO selector ----
        println("\n########## SECTION 3: slot-83 selector FUN_18241785c (CryFile vs CIO) ##########");
        decompPrint(0x18241785cL, "FUN_18241785c slot83 GetFileData method-log selector", 120);

        // ---- 4. direct callers of FGetCachedFileData (slot 40 body 0x51CD00) ----
        println("\n########## SECTION 4: direct callers of FGetCachedFileData 0x51CD00 ##########");
        Address fgcfd = a(0x18051cd00L);
        Reference[] fgRefs = getReferencesTo(fgcfd);
        println("  direct refs to 0x51CD00: " + fgRefs.length);
        LinkedHashSet<Long> fgCallers = new LinkedHashSet<>();
        for (Reference rf : fgRefs) {
            println("    <- " + fnLabel(rf.getFromAddress()) + "  (refType " + rf.getReferenceType() + ")");
            Function cf = getFunctionContaining(rf.getFromAddress());
            if (cf != null) fgCallers.add(cf.getEntryPoint().getOffset());
        }

        println("\ndone.");
    }
}
