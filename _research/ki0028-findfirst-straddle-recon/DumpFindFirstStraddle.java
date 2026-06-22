// KI-0028 — FindFirst (slot 63) straddle hunt.
//
// kcdx FindFirst returns a small int (id<<1)|1; engine FindFirst (FUN_180973058)
// returns a REFCOUNTED CCryPakFindData* object. A boot consumer that does more
// than opaque pass-back ( -1<h  +  FindNext/FindClose) operates a wild pointer.
//
// THREE JOBS:
//  J1 — decompile + disasm engine FindNext 0x18041d640 + FindClose 0x18097383c,
//       to show HOW the OBJECT handle is operated (deref obj[1] refcount / obj[3]
//       entry-list / object-release).
//  J2 — find ALL call sites of slot 63 (vtable +0x1F8). Two routes:
//       (a) DATA xref to the slot in CCryPak vtable 0x183A95FA8+0x1F8 -> nothing,
//           the slot holds the fn ptr; instead scan code for `call [reg+0x1F8]`.
//       (b) direct calls to engine FindFirst 0x180973058.
//       For each caller decompile + classify: opaque (-1<h, pass to 64/65) vs
//       straddle (deref/refcount/non-65 release).
//  J3 — flag the single most-likely boot straddle consumer.
//@category KCD2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.util.*;

public class DumpFindFirstStraddle extends GhidraScript {
    AddressSpace sp; Memory mem; DecompInterface di; java.io.PrintWriter fw;
    void out(String s){ super.println(s); if(fw!=null){ fw.println(s); fw.flush(); } }
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    String decomp(Function f){
        if(f==null) return "(null fn)";
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        if(r!=null && r.decompileCompleted() && r.getDecompiledFunction()!=null)
            return r.getDecompiledFunction().getC();
        return "(decompile failed)";
    }
    Function fnAt(long va){ return getFunctionContaining(a(va)); }

    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface(); di.openProgram(currentProgram);
        fw = new java.io.PrintWriter(new java.io.FileWriter(
            "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\ki0028-findfirst-straddle-recon\\_dump.txt"));

        long VT = 0x183A95FA8L;
        out("=== CCryPak vtable @0x183A95FA8 find triplet ===");
        out(String.format("  +0x1F8 (slot63 FindFirst) = 0x%X", ptr(VT+0x1F8)));
        out(String.format("  +0x200 (slot64 FindNext)  = 0x%X", ptr(VT+0x200)));
        out(String.format("  +0x208 (slot65 FindClose) = 0x%X", ptr(VT+0x208)));

        // ---- J1: FindNext + FindClose bodies ----
        for(long va : new long[]{0x18041d640L, 0x18097383cL}){
            Function f = fnAt(va);
            out("\n\n========== J1 engine fn @0x"+Long.toHexString(va)+" ("+(f!=null?f.getName():"?")+") ==========");
            out(decomp(f));
        }
        // also FindFirst itself for the object-return shape
        out("\n\n========== J1b engine FindFirst @0x180973058 ==========");
        out(decomp(fnAt(0x180973058L)));

        // ---- J2: find all `call [reg+0x1F8]` sites across the image ----
        out("\n\n========== J2 scan: indirect call [reg+0x1F8] sites ==========");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Set<Long> callerFns = new LinkedHashSet<>();
        int n1f8 = 0;
        while(it.hasNext()){
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            if(!m.equals("CALL")) continue;
            String t = ins.toString();
            // match the +0x1f8 displacement in an indirect memory operand
            if(t.contains("+ 0x1f8]") || t.contains("+0x1f8]") || t.contains("0x1f8]")){
                Function cf = getFunctionContaining(ins.getAddress());
                long cfa = (cf!=null)? cf.getEntryPoint().getOffset() : 0;
                out(String.format("  call+0x1f8 @0x%X  in %s : %s",
                    ins.getAddress().getOffset(), (cf!=null?cf.getName():"?"), t));
                if(cf!=null) callerFns.add(cfa);
                n1f8++;
            }
        }
        out("  total call+0x1f8 sites = "+n1f8+" ; distinct caller fns = "+callerFns.size());

        // direct calls to engine FindFirst 0x180973058
        out("\n========== J2b direct call refs to engine FindFirst 0x180973058 ==========");
        ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a(0x180973058L));
        while(ri.hasNext()){
            Reference r = ri.next();
            out(String.format("  ref %s from 0x%X", r.getReferenceType(), r.getFromAddress().getOffset()));
            Function cf = getFunctionContaining(r.getFromAddress());
            if(cf!=null) callerFns.add(cf.getEntryPoint().getOffset());
        }

        // ---- decompile each caller fn (skip the 2 KI-0027 known opaque ones) ----
        long KNOWN1 = 0x180974484L, KNOWN2 = 0x18041d238L;
        out("\n\n========== J2c caller bodies (classify opaque vs straddle) ==========");
        out("  (known opaque from KI-0027: FUN_180974484 table-glob, FUN_18041d238 listing)");
        for(long cfa : callerFns){
            Function f = fnAt(cfa);
            String tag = (cfa==KNOWN1||cfa==KNOWN2) ? " [KI-0027 KNOWN-OPAQUE]" : "";
            out("\n----- caller FUN_0x"+Long.toHexString(cfa)+tag+" -----");
            out(decomp(f));
        }
        out("\n=== DONE ===");
        fw.close();
    }
}
