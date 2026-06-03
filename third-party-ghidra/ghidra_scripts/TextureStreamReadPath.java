// FRONT F1 part 2: trace the TEXTURE-STREAMING read primitive.
// SECTION-1 discovery proved: NO CreateFileMapping/MapViewOfFile anywhere (not mmap'd);
// only async-IO marker present = GetOverlappedResult. So .dds is either FOpen+FRead or
// streamed (StreamEngine / async). This script reads the bodies to decide which.
//
// Anchors (from discovery):
//   FUN_1808654b4 = CTexture::StreamPrepare ("Failed to allocate ... persistent mip chain")
//   FUN_1805f9df4 = the TextureStreaming.cpp consumer
//   FUN_180865... family = CTexture stream
// Also: find GetOverlappedResult's caller (the async read completion) and whether the
//   StreamEngine read path reaches CCryPak::FOpen(0x4614A0) / FRead(0x51CD00) / FReadRaw(0x51E1F8)
//   or a separate async file-read primitive (CreateFile + ReadFile + overlapped).
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.listing.Data;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class TextureStreamReadPath extends GhidraScript {
    AddressSpace sp; Memory mem; DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    String fnLabel(Address from) {
        Function cf = getFunctionContaining(from);
        if (cf == null) return "<no-fn> @" + from;
        return cf.getName() + " @0x" + Long.toHexString(cf.getEntryPoint().getOffset());
    }

    void decompPrint(long fva, String label) throws Exception {
        Function f = getFunctionAt(a(fva));
        if (f == null) f = getFunctionContaining(a(fva));
        println("\n================================================================================");
        println(label + " @0x" + Long.toHexString(fva));
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION"); return; }
        println("  proto: " + f.getPrototypeString(true,false) + "  size(addrs)=" + f.getBody().getNumAddresses());
        DecompileResults dr = di.decompileFunction(f, 180, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) println(dr.getDecompiledFunction().getC());
        else println("  (decompile failed: " + (dr!=null?dr.getErrorMessage():"null") + ")");
    }

    // print callers of an address (who references it)
    void callersOf(long va, String label) {
        println("\n---- callers/refs of " + label + " @0x" + Long.toHexString(va) + " ----");
        Reference[] rs = getReferencesTo(a(va));
        println("  " + rs.length + " ref(s)");
        LinkedHashSet<String> seen = new LinkedHashSet<>();
        for (Reference rf : rs) seen.add(fnLabel(rf.getFromAddress()) + "  (" + rf.getReferenceType() + ", from " + rf.getFromAddress() + ")");
        for (String s : seen) println("    <- " + s);
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // 0. async-IO + file-open Win32 imports and their callers
        println("########## async/file-open Win32 imports + callers ##########");
        String[] apis = {"GetOverlappedResult","ReadFile","ReadFileEx","CreateFileA","CreateFileW",
                         "CreateFile2","SetFilePointer","SetFilePointerEx","CreateIoCompletionPort",
                         "GetQueuedCompletionStatus","_wfopen","fopen"};
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        Map<String,List<Address>> sa = new LinkedHashMap<>();
        for (String n : apis) sa.put(n, new ArrayList<>());
        while (it.hasNext()) {
            Symbol s = it.next();
            if (sa.containsKey(s.getName())) sa.get(s.getName()).add(s.getAddress());
        }
        for (String n : apis) {
            List<Address> as = sa.get(n);
            if (as.isEmpty()) { println("  " + n + ": absent"); continue; }
            for (Address x : as) callersOf(x.getOffset(), n);
        }

        // 1. the texture-streaming anchors
        println("\n########## texture-stream anchor decompiles ##########");
        decompPrint(0x1808654b4L, "CTexture::StreamPrepare (alloc mip chain)");
        decompPrint(0x1805f9df4L, "TextureStreaming.cpp consumer FUN_1805f9df4");

        // 2. who calls the StreamEngine? Look for a string anchor of the stream engine read.
        println("\n########## StreamEngine / IStreamEngine read string anchors ##########");
        String[] needles = {"StreamEngine","IStreamEngine","StreamReadParams","StartRead",
                            "CStreamEngine","ReadStream","StreamData","g_pIStreamEngine",
                            "StreamingIO","CAsyncIOFileRequest","CReadStream","StreamAsyncOnComplete"};
        ghidra.program.model.listing.DataIterator dataIt = currentProgram.getListing().getDefinedData(true);
        Map<String,List<Address>> hit = new LinkedHashMap<>();
        while (dataIt.hasNext()) {
            Data d = dataIt.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String svv = (String) v;
            for (String nd : needles) if (svv.contains(nd)) { hit.computeIfAbsent(nd,k->new ArrayList<>()).add(d.getAddress()); break; }
        }
        for (Map.Entry<String,List<Address>> e : hit.entrySet()) {
            println("\n  needle \"" + e.getKey() + "\": " + e.getValue().size());
            int shown=0;
            for (Address x : e.getValue()) {
                Reference[] rs = getReferencesTo(x);
                Data d = getDataAt(x);
                String txt = (d!=null && d.getValue() instanceof String) ? (String)d.getValue() : "?";
                println("    @" + x + " \"" + txt + "\" ("+rs.length+" xref)");
                for (Reference rf : rs) println("        <- " + fnLabel(rf.getFromAddress()));
                if (++shown>=6){ println("    ...more"); break; }
            }
        }

        println("\ndone.");
    }
}
