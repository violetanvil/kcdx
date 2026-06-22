// KI-0028 — pin GENUINE find-triplet consumers: a fn that calls [reg+0x1f8]
// AND ([reg+0x200] OR [reg+0x208]) on the SAME receiver pattern is a real
// FindFirst/FindNext/FindClose user (the +0x1f8-only hits are vtable false
// positives). For each, decompile + classify opaque vs straddle, and detect
// whether the handle is dereferenced (h[...], refcount, non-65 release).
//@category KCD2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class DumpTripletConsumers extends GhidraScript {
    AddressSpace sp; DecompInterface di; java.io.PrintWriter fw;
    void out(String s){ super.println(s); if(fw!=null){ fw.println(s); fw.flush(); } }
    Address a(long va){ return sp.getAddress(va); }
    String decomp(Function f){
        if(f==null) return "(null)";
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if(r!=null && r.decompileCompleted() && r.getDecompiledFunction()!=null)
            return r.getDecompiledFunction().getC();
        return "(decompile failed)";
    }

    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        di = new DecompInterface(); di.openProgram(currentProgram);
        fw = new java.io.PrintWriter(new java.io.FileWriter(
            "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-findfirst-straddle-recon\\_triplet.txt"));

        // per-fn flags: has +0x1f8, +0x200, +0x208 indirect calls
        Map<Long,boolean[]> flags = new HashMap<>();   // [0]=1f8 [1]=200 [2]=208
        Map<Long,String> name = new HashMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while(it.hasNext()){
            Instruction ins = it.next();
            if(!ins.getMnemonicString().equals("CALL")) continue;
            String t = ins.toString().toLowerCase();
            int idx=-1;
            if(t.contains("+ 0x1f8]")) idx=0;
            else if(t.contains("+ 0x200]")) idx=1;
            else if(t.contains("+ 0x208]")) idx=2;
            else continue;
            Function cf = getFunctionContaining(ins.getAddress());
            if(cf==null) continue;
            long e = cf.getEntryPoint().getOffset();
            flags.computeIfAbsent(e, k->new boolean[3])[idx]=true;
            name.put(e, cf.getName());
        }
        // a genuine triplet consumer: has 1f8 AND (200 OR 208)
        List<Long> consumers = new ArrayList<>();
        for(Map.Entry<Long,boolean[]> en : flags.entrySet()){
            boolean[] b = en.getValue();
            if(b[0] && (b[1]||b[2])) consumers.add(en.getKey());
        }
        Collections.sort(consumers);
        out("=== GENUINE find-triplet consumers (call 1f8 AND 200/208) ===");
        out("  count = "+consumers.size());
        long KNOWN1=0x180974484L, KNOWN2=0x18041d238L;
        for(long e : consumers){
            boolean[] b = flags.get(e);
            String tag = (e==KNOWN1)?" [KI-0027 table-glob OPAQUE]":(e==KNOWN2)?" [KI-0027 listing OPAQUE]":"";
            out(String.format("  FUN_0x%X  1f8=%b 200=%b 208=%b%s", e, b[0],b[1],b[2], tag));
        }
        out("\n\n=== bodies of the NON-KNOWN consumers ===");
        for(long e : consumers){
            if(e==KNOWN1||e==KNOWN2) continue;
            out("\n\n----- FUN_0x"+Long.toHexString(e)+" -----");
            out(decomp(getFunctionContaining(a(e))));
        }
        out("\n=== DONE ===");
        fw.close();
    }
}
