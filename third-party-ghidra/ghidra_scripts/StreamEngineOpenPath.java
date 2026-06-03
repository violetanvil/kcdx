// asset-loadpath-map-recon F5: does the CryEngine streaming engine open files
// through CCryPak::FOpen (slot 36, 0x4614A0) or via its OWN Win32 handle source
// (CreateFileA/W / overlapped ReadFile / a direct fopen) that BYPASSES CCryPak?
//
// Method (static, read-only):
//  S1. Enumerate Win32 file-IO imports (CreateFileA/W, ReadFile, ReadFileEx,
//      GetOverlappedResult, CreateFileMappingW, MapViewOfFile) + every caller fn.
//  S2. Find streaming string anchors (StreamEngine / Streaming / async / directstorage)
//      and the functions that reference them.
//  S3. For each Win32-file-open caller AND each streaming-anchored fn, decompile and
//      classify: does the body reach CCryPak::FOpen (call to vtable +0x120, or a call
//      to 0x4614A0), OR call a Win32 file-open import directly (CreateFile/ReadFile)?
//      A fn that calls CreateFile/ReadFile and does NOT route through FOpen is a BYPASS
//      candidate; report it with the cited call lines.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.util.*;

public class StreamEngineOpenPath extends GhidraScript {
    AddressSpace sp;
    DecompInterface di;
    Address a(long va){ return sp.getAddress(va); }

    static final long FOPEN_RVA = 0x180461304L; // NOTE set below; placeholder
    static final long IMAGE_BASE = 0x180000000L;
    static final long FOPEN_VA = 0x1804614a0L;     // CCryPak::FOpen slot 36
    static final long FREOPEN_VA = 0x180461304L;   // slot 38 FOpen-by-pak-index / FReopen

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // ---------- S1: Win32 file-IO import callers ----------
        String[] fileApis = {
            "CreateFileA","CreateFileW","CreateFile2","ReadFile","ReadFileEx",
            "GetOverlappedResult","GetOverlappedResultEx",
            "CreateFileMappingA","CreateFileMappingW","MapViewOfFile","MapViewOfFileEx",
            "CreateIoCompletionPort","DeviceIoControl"
        };
        println("########## S1: Win32 file-IO import callers ##########");
        // map: callerFn entry -> set of apis it calls
        TreeMap<Long,Set<String>> win32Callers = new TreeMap<>();
        SymbolTable st = currentProgram.getSymbolTable();
        for (String api : fileApis) {
            int found = 0;
            SymbolIterator it = st.getSymbols(api);
            while (it.hasNext()) {
                Symbol s = it.next();
                Address sa = s.getAddress();
                Reference[] refs = getReferencesTo(sa);
                for (Reference rf : refs) {
                    Function cf = getFunctionContaining(rf.getFromAddress());
                    if (cf == null) continue;
                    long e = cf.getEntryPoint().getOffset();
                    win32Callers.computeIfAbsent(e, k->new TreeSet<>()).add(api);
                    found++;
                }
            }
            println("  " + api + ": " + found + " call refs");
        }
        println("  distinct fns calling a Win32 file API: " + win32Callers.size());

        // ---------- S2: streaming string anchors ----------
        println("\n########## S2: streaming string anchors + referencing fns ##########");
        String[] anchors = {
            "StreamEngine","IStreamEngine","CStreamEngine","Streaming","stream engine",
            "DirectStorage","directstorage","StreamReadBatch","AsyncFile","async",
            "overlapped","Overlapped","UncachedStream","StreamReadParams","StreamingIO",
            "g_pIStreamEngine","CAsyncIOFilePool"
        };
        TreeMap<Long,Set<String>> strFns = new TreeMap<>();
        DataIterator di2 = currentProgram.getListing().getDefinedData(true);
        int scanned = 0;
        while (di2.hasNext()) {
            Data d = di2.next();
            if (d == null) continue;
            DataType dt = d.getDataType();
            if (dt == null) continue;
            String tn = dt.getName().toLowerCase();
            if (!(tn.contains("string") || tn.contains("char") || tn.contains("unicode"))) continue;
            Object val = d.getValue();
            if (!(val instanceof String)) continue;
            String sv = (String) val;
            scanned++;
            for (String anc : anchors) {
                if (sv.contains(anc)) {
                    Address strAddr = d.getAddress();
                    ReferenceIterator ri = getReferenceIterator(strAddr);
                    boolean any=false;
                    while (ri.hasNext()) {
                        Reference rf = ri.next();
                        Function cf = getFunctionContaining(rf.getFromAddress());
                        if (cf != null) {
                            strFns.computeIfAbsent(cf.getEntryPoint().getOffset(), k->new TreeSet<>())
                                  .add(anc + "='" + truncate(sv,48) + "'");
                            any=true;
                        }
                    }
                    if (!any) {
                        // record the string even with no direct fn xref (interned-late)
                        println("  [no-fn-xref] @0x"+Long.toHexString(strAddr.getOffset())+
                                " anchor '"+anc+"' in: "+truncate(sv,80));
                    }
                    break; // one anchor per string is enough
                }
            }
        }
        println("  defined strings scanned: " + scanned);
        println("  distinct fns referencing a streaming-anchor string: " + strFns.size());
        for (Map.Entry<Long,Set<String>> en : strFns.entrySet()) {
            println("    FN 0x"+Long.toHexString(en.getKey())+"  "+en.getValue());
        }

        // ---------- S3: classify the union of candidate fns ----------
        println("\n########## S3: classify candidate fns (BYPASS vs routes-through-FOpen) ##########");
        TreeSet<Long> candidates = new TreeSet<>();
        candidates.addAll(win32Callers.keySet());
        candidates.addAll(strFns.keySet());
        println("  total candidate fns to classify: " + candidates.size());

        int bypass=0, viaFOpen=0, neither=0;
        for (long fva : candidates) {
            Function f = getFunctionAt(a(fva));
            if (f == null) continue;
            DecompileResults r = di.decompileFunction(f, 45, monitor);
            if (r == null || !r.decompileCompleted()) continue;
            String c = r.getDecompiledFunction().getC();

            boolean callsWin32Open = mentionsAny(c, new String[]{"CreateFileA","CreateFileW","CreateFile2","ReadFile","ReadFileEx","GetOverlappedResult"});
            boolean callsMap = mentionsAny(c, new String[]{"CreateFileMapping","MapViewOfFile"});
            boolean callsFopenSlot = c.contains("+ 0x120)") || c.contains("0x120))(") || c.contains("FUN_1804614a0") || c.contains("1804614a0");
            boolean callsFReopenSlot = c.contains("+ 0x130)") || c.contains("FUN_180461304") || c.contains("180461304");
            boolean callsCrt = mentionsAny(c, new String[]{"fopen","_wfopen","fopen_s","fread","_read","_open"});

            Set<String> w32 = win32Callers.get(fva);
            Set<String> anc = strFns.get(fva);
            String tag = (w32!=null?(" win32="+w32):"") + (anc!=null?(" anchor="+anc):"");

            String cls;
            if ((callsWin32Open||callsMap) && !callsFopenSlot && !callsFReopenSlot) { cls="BYPASS-CANDIDATE"; bypass++; }
            else if (callsFopenSlot || callsFReopenSlot) { cls="via-FOpen/FReopen"; viaFOpen++; }
            else { cls="neither(indirect)"; neither++; }

            // only print the interesting ones: any streaming-anchored fn, and any bypass candidate
            if (anc!=null || cls.equals("BYPASS-CANDIDATE")) {
                println("\n=== FN "+f.getName()+" @0x"+Long.toHexString(fva)+"  ["+cls+"]"+tag+" ===");
                String[] lines = c.split("\n");
                for (int i=0;i<lines.length;i++){
                    String ln = lines[i];
                    if (mentionsAny(ln, new String[]{"CreateFile","ReadFile","GetOverlappedResult","MapViewOfFile","CreateFileMapping","0x120)","0x120))(","1804614a0","180461304","fopen","_wfopen","fread"})) {
                        println("   :"+i+"  "+ln.trim());
                    }
                }
            }
        }
        println("\n########## SUMMARY ##########");
        println("  candidate fns: "+candidates.size());
        println("  BYPASS-CANDIDATE (Win32 open/read, no FOpen edge): "+bypass);
        println("  via-FOpen/FReopen: "+viaFOpen);
        println("  neither (indirect / helper): "+neither);
        println("done.");
    }

    boolean mentionsAny(String hay, String[] needles){
        for (String n : needles) if (hay.contains(n)) return true;
        return false;
    }
    String truncate(String s, int n){ return s.length()<=n? s : s.substring(0,n)+"..."; }
    ReferenceIterator getReferenceIterator(Address to){
        return currentProgram.getReferenceManager().getReferencesTo(to);
    }
}
