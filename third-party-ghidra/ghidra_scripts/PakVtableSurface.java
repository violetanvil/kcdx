// Phase 8.5 FRONT-1: map the FULL CCryPak (ICryPak) vtable surface @ 0x183A95FA8.
//
// Goal: a complete slot -> RVA -> ROLE table -- every method a file-consuming caller
// might route through, so kcdx knows the complete hook/replace surface.
//
// Prior work (_searchpath_api_raw.txt) already dumped slots 0..95 (slot/RVA/fn) but
// (a) capped the loop at 96 so the true vtable END is unknown, and (b) named no roles.
// This script: dumps to the true non-exec end (cap 200), and for EVERY slot emits a
// compact role fingerprint from the decompile -- param count, return shape, the leaf
// calls it makes, the CCryPak member offsets it touches, and a body-feature token scan
// (strlen / seek / read / write / size / find / folder / archive / CryStringT) -- plus
// any string literals the body references (AP3: role anchored on resolved strings/calls,
// not a canonical header). Slots already role-mapped by prior fronts (1,19,20,21,36,41,55)
// are still fingerprinted here for a uniform table + cross-check.
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
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class PakVtableSurface extends GhidraScript {
    AddressSpace sp;
    Memory mem;
    DecompInterface di;
    static final long BASE = 0x180000000L;
    Address a(long va){ return sp.getAddress(va); }
    long ptr(long va) throws Exception { return mem.getLong(a(va)); }

    String decompC(Function f) {
        if (f == null) return null;
        DecompileResults r = di.decompileFunction(f, 45, monitor);
        if (r != null && r.decompileCompleted()) return r.getDecompiledFunction().getC();
        return null;
    }

    // tokens we scan the decompiled body for -> role hint
    static final String[][] FEAT = {
        {"strlen", "STRLEN"},
        {"0x7ff", "PATHCAP2048"},      // CryEngine 2048-byte path cap (FOpen-family)
        {"fseek", "SEEK"}, {"Seek", "SEEK"},
        {"fread", "READ"}, {"memcpy", "MEMCPY"},
        {"fwrite", "WRITE"},
        {"feof", "EOF"}, {"ferror", "FERROR"}, {"ftell", "TELL"},
        {"+ 0x198", "SP_VEC"}, {"+ 0x1a0", "SP_VEC"},     // search-path vector
        {"+ 0x1b0", "ALIAS"}, {"+ 0x1b8", "ALIAS"},       // alias table
        {"+ 0xf0", "PAKARR"}, {"+ 0x120", "PAKARR"},      // loaded-pak array (resolver)
        {"+ 0x228", "OSFILE"},                            // OS file-attr object
        {"+ 0x188", "DATAROOT"},                          // game data root CryStringT
    };

    String fingerprint(long va, Function f) {
        String c = decompC(f);
        StringBuilder fb = new StringBuilder();
        Set<String> feats = new LinkedHashSet<>();
        if (c != null) {
            for (String[] t : FEAT) if (c.contains(t[0])) feats.add(t[1]);
        }
        // return shape: void vs value. crude: does the proto start with "void"
        String proto = "?";
        int pc = -1;
        if (f != null) { pc = f.getParameterCount(); }
        if (c != null) {
            int nl = c.indexOf('\n');
            // find the signature line (the line with the function name + '(')
            for (String line : c.split("\n")) {
                if (line.contains("FUN_" + Long.toHexString(va).substring(0)) || line.contains(f.getName()+"(")) { proto = line.trim(); break; }
            }
            if (proto.equals("?")) {
                // fallback: first line containing '(' and ')'
                for (String line : c.split("\n")) { if (line.contains("(") && line.contains(")") && (line.contains("void")||line.contains("undefined")||line.contains("ulong")||line.contains("int")||line.contains("longlong"))) { proto = line.trim(); break; } }
            }
        }
        boolean retVoid = proto.startsWith("void ");
        fb.append("params=").append(pc);
        fb.append(retVoid ? " ret=void" : " ret=val");
        fb.append(" feats=").append(feats.isEmpty() ? "-" : String.join(",", feats));
        return fb.toString();
    }

    // collect the (up to N) leaf call targets the body makes, by name -- disambiguates
    // thin forwarders (slot X just tail-calls FUN_Y).
    String leafCalls(Function f, int max) {
        if (f == null) return "-";
        List<String> calls = new ArrayList<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext() && calls.size() < max) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().equals("CALL") && !ins.getMnemonicString().equals("JMP")) continue;
            Reference[] rfs = ins.getReferencesFrom();
            for (Reference rf : rfs) {
                Function tf = getFunctionAt(rf.getToAddress());
                if (tf != null) {
                    String nm = tf.getName() + (ins.getMnemonicString().equals("JMP") ? "(tail)" : "");
                    if (!calls.contains(nm)) calls.add(nm);
                }
            }
        }
        return calls.isEmpty() ? "-" : String.join(" ", calls);
    }

    // strings the body references (resolved, for AP3 anchoring)
    String bodyStrings(Function f, int max) {
        if (f == null) return "";
        List<String> out = new ArrayList<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext() && out.size() < max) {
            Instruction ins = it.next();
            Reference[] rfs = ins.getReferencesFrom();
            for (Reference rf : rfs) {
                Data d = getDataAt(rf.getToAddress());
                if (d != null && d.hasStringValue()) {
                    String s = d.getDefaultValueRepresentation();
                    if (s != null && s.length() > 2 && !out.contains(s)) out.add(s);
                }
            }
        }
        return out.isEmpty() ? "" : ("  STR=" + String.join(" | ", out));
    }

    @Override
    public void run() throws Exception {
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        mem = currentProgram.getMemory();
        di = new DecompInterface();
        di.openProgram(currentProgram);

        long vt = 0x183A95FA8L;
        println("================ CCryPak vtable surface @ 0x" + Long.toHexString(vt) + " ================");

        for (int i = 0; i < 200; i++) {
            long target;
            try { target = ptr(vt + 8L*i); } catch (Exception e) { println("slot " + i + ": <read fail -> end>"); break; }
            MemoryBlock b = mem.getBlock(a(target));
            boolean exec = (b != null && b.isExecute());
            if (!exec) {
                println("slot " + i + " (+0x" + Long.toHexString(8L*i) + ") = 0x" + Long.toHexString(target)
                    + "  <non-exec -> VTABLE END at slot " + i + ">");
                break;
            }
            Function f = getFunctionAt(a(target));
            if (f == null) f = getFunctionContaining(a(target));
            long fva = (f != null) ? f.getEntryPoint().getOffset() : target;
            String fp = fingerprint(target, f);
            String leaves = leafCalls(f, 5);
            String strs = bodyStrings(f, 4);
            println("slot " + i + " +0x" + Long.toHexString(8L*i)
                + " RVA 0x" + Long.toHexString(target - BASE)
                + (f != null ? " " + f.getName() : " (no fn)")
                + " | " + fp
                + " | leaves: " + leaves
                + strs);
        }
        println("\n================ done ================");
    }
}
