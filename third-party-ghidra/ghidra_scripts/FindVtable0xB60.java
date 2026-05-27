// Find vtable for class with +0xB60 sub-object referenced from FUN_1805616e8.
// Strategy: find all instructions storing a register (RAX, RCX, RDX, ...) to [reg+0xB60],
// and for each, look back up to 64 bytes for a `lea SAME_REG, [rip+disp32]`.
// Run via: analyzeHeadless <proj_dir> KCD2 -process WHGame.dll -postScript FindVtable0xB60.java -noanalysis -readOnly
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
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.ConsoleTaskMonitor;

import java.util.*;

public class FindVtable0xB60 extends GhidraScript {

    static final long BASE = 0x180000000L;

    String hex(byte[] b, int n) {
        StringBuilder sb = new StringBuilder();
        int m = Math.min(n, b.length);
        for (int i = 0; i < m; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    // Compute the destination register name for a `mov [r/m + 0xB60], reg` x86_64 instr.
    // Returns null if pattern doesn't match.
    // We focus on the form: REX(0x48/0x49/0x4C/0x4D) 0x89 modrm disp32
    // modrm = 10_rrr_bbb (mod=10 disp32). rrr = source reg (the one being stored), bbb = base reg.
    String classifySrcReg(byte rex, byte modrm) {
        int r = (rex & 0x04) != 0 ? 8 : 0; // REX.R extends modrm.rrr
        int rrr = (modrm >> 3) & 0x7;
        int srcIdx = r | rrr;
        String[] names = {"RAX","RCX","RDX","RBX","RSP","RBP","RSI","RDI",
                          "R8","R9","R10","R11","R12","R13","R14","R15"};
        return names[srcIdx];
    }

    // The encoding for `mov [reg+disp32], reg` with mod=10 is 7 bytes total: REX 89 modrm disp32
    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();

        println("================================================================================");
        println("Hunting for vtables stored at offset +0xB60");
        println("================================================================================");

        // 1. Find all `48/49/4C/4D  89  ?? 60 0B 00 00` patterns -- mov [reg+0xB60], reg (REX W or REX WR or REX WB or REX WRB).
        // modrm.bbb selects the base register, modrm.rrr selects the src.
        // We'll iterate executable blocks and search.

        List<Address> mov0xB60Sites = new ArrayList<>();
        int[] rexVariants = {0x48, 0x49, 0x4C, 0x4D};
        for (int rex : rexVariants) {
            // For each ModR/M with mod=10 (=disp32), there are 64 combinations of rrr/bbb.
            // It's faster to just search for `REX 0x89 ?? 0x60 0x0B 0x00 0x00` -- but findBytes doesn't
            // support wildcards directly; we'll do it manually by scanning for `REX 0x89` then
            // checking subsequent bytes.
            byte[] needle = new byte[] { (byte)rex, (byte)0x89 };
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized() || !blk.isExecute()) continue;
                Address a = mem.findBytes(blk.getStart(), blk.getEnd(), needle, null, true, monitor);
                while (a != null) {
                    // Check bytes at a+2, a+3, a+4, a+5, a+6
                    try {
                        int modrm = mem.getByte(a.add(2)) & 0xFF;
                        int b3 = mem.getByte(a.add(3)) & 0xFF;
                        int b4 = mem.getByte(a.add(4)) & 0xFF;
                        int b5 = mem.getByte(a.add(5)) & 0xFF;
                        int b6 = mem.getByte(a.add(6)) & 0xFF;
                        // mod must be 10 (disp32). modrm top bits = 10 => 0x80..0xBF.
                        // BUT we don't want mod=01 or mod=00; we explicitly check.
                        int mod = (modrm >> 6) & 0x3;
                        if (mod == 2 && b3 == 0x60 && b4 == 0x0B && b5 == 0x00 && b6 == 0x00) {
                            mov0xB60Sites.add(a);
                        }
                    } catch (Exception e) {
                        // skip
                    }
                    Address next = a.add(1);
                    if (next.compareTo(blk.getEnd()) > 0) break;
                    a = mem.findBytes(next, blk.getEnd(), needle, null, true, monitor);
                }
            }
        }
        println("Total `mov [reg+0xB60], reg` (mod=10) sites: " + mov0xB60Sites.size());

        // 2. For each site, look back up to 96 bytes for `lea SAME_REG, [rip+disp32]`.
        // SAME_REG must match the source register of the mov.
        // lea encoding: REX 8D modrm disp32. mod=00 with bbb=101 means RIP-relative.
        // So we look for: 48/4C 8D ?? where modrm = 00_rrr_101 => 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D
        //   for RAX..RDI (REX.R=0)
        //   or 0x05, 0x0D, ... with REX.R=1 for R8..R15

        Map<Long, List<Address>> vtableCands = new TreeMap<>();
        int withLea = 0;
        for (Address site : mov0xB60Sites) {
            // Decode the mov to get its source reg.
            byte rex = (byte)(mem.getByte(site) & 0xFF);
            byte modrm = (byte)(mem.getByte(site.add(2)) & 0xFF);
            int srcRegIdx = (((rex & 0x04) != 0 ? 8 : 0) | ((modrm >> 3) & 0x7));
            // baseRegIdx for info
            int baseRegIdx = (((rex & 0x01) != 0 ? 8 : 0) | (modrm & 0x7));

            // Look backwards 4..96 bytes for a `lea SAME_REG, [rip+disp32]`
            // The `lea` instr is 7 bytes (REX + 8D + modrm + 4 disp).
            // We scan by trying offsets back of 7..96 where the candidate `lea` starts.
            for (int back = 7; back <= 96; back++) {
                Address probe;
                try { probe = site.subtract(back); } catch (Exception e) { break; }
                // verify the lea starts at probe and ends at probe+7 (which we want <= site)
                int leaRex, leaOp, leaMod;
                try {
                    leaRex = mem.getByte(probe) & 0xFF;
                    leaOp  = mem.getByte(probe.add(1)) & 0xFF;
                    leaMod = mem.getByte(probe.add(2)) & 0xFF;
                } catch (Exception e) { continue; }
                if (!(leaRex == 0x48 || leaRex == 0x4C)) continue;
                if (leaOp != 0x8D) continue;
                // mod=00, bbb=101 => modrm = 00_rrr_101 = 0x05 | (rrr<<3)
                int leaMod00 = (leaMod >> 6) & 0x3;
                int leaBbb = leaMod & 0x7;
                int leaRrr = (leaMod >> 3) & 0x7;
                if (leaMod00 != 0 || leaBbb != 5) continue;
                int leaDestIdx = (((leaRex & 0x04) != 0 ? 8 : 0) | leaRrr);
                if (leaDestIdx != srcRegIdx) continue;
                // Found matching lea!
                int d0 = mem.getByte(probe.add(3)) & 0xFF;
                int d1 = mem.getByte(probe.add(4)) & 0xFF;
                int d2 = mem.getByte(probe.add(5)) & 0xFF;
                int d3 = mem.getByte(probe.add(6)) & 0xFF;
                int disp = d0 | (d1 << 8) | (d2 << 16) | (d3 << 24);
                long ripAfter = probe.add(7).getOffset();
                long vtVa = ripAfter + (long)disp;
                vtableCands.computeIfAbsent(vtVa, k -> new ArrayList<>()).add(site);
                withLea++;
                break;
            }
        }
        println("Sites with matching lea backref: " + withLea);
        println("Distinct vtable VAs: " + vtableCands.size());

        // 3. Filter candidate vtables: they must be in .rdata (read-only data section), readable,
        //    and slot[0] must be a function-looking address (in .text).
        long textStart = 0, textEnd = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (blk.getName().equalsIgnoreCase(".text") || blk.isExecute()) {
                if (textStart == 0) textStart = blk.getStart().getOffset();
                long e = blk.getEnd().getOffset();
                if (e > textEnd) textEnd = e;
            }
        }
        println(String.format("\n.text range (approx): 0x%X - 0x%X", textStart, textEnd));

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        for (Map.Entry<Long, List<Address>> entry : vtableCands.entrySet()) {
            long vtVa = entry.getKey();
            Address vtAddr;
            try { vtAddr = sp.getAddress(vtVa); } catch (Exception e) { continue; }
            long s0, s1, s2, s3, s4;
            try {
                s0 = mem.getLong(vtAddr);
                s1 = mem.getLong(vtAddr.add(8));
                s2 = mem.getLong(vtAddr.add(16));
                s3 = mem.getLong(vtAddr.add(24));
                s4 = mem.getLong(vtAddr.add(32));
            } catch (Exception ex) { continue; }
            // sanity: all four slots in .text
            boolean validVt = (s0 >= textStart && s0 < textEnd) && (s1 >= textStart && s1 < textEnd);
            println(String.format("\n=== vtable @ 0x%X  (%d constructor sites)  valid=%s ===",
                    vtVa, entry.getValue().size(), validVt));
            Symbol vtSym = st.getPrimarySymbol(vtAddr);
            if (vtSym != null) println("  vtable symbol: " + vtSym.getName());
            println(String.format("  slot[0] = 0x%X", s0));
            println(String.format("  slot[1] = 0x%X  <-- TARGET", s1));
            println(String.format("  slot[2] = 0x%X", s2));
            println(String.format("  slot[3] = 0x%X", s3));
            println(String.format("  slot[4] = 0x%X", s4));

            // Show constructor sites and the enclosing functions
            int shown = 0;
            for (Address csite : entry.getValue()) {
                if (shown++ >= 4) break;
                Function f = getFunctionContaining(csite);
                println(String.format("  ctor site @%s in %s", csite, f != null ? f.getName() + "@" + f.getEntryPoint() : "<none>"));
            }

            if (!validVt) continue;

            // Decode slot[1]
            Address slot1Addr = sp.getAddress(s1);
            Function fn = getFunctionAt(slot1Addr);
            if (fn == null) fn = getFunctionContaining(slot1Addr);
            if (fn != null) {
                println(String.format("\n  --- slot[1] function: %s @ %s  size=%d ---",
                        fn.getName(), fn.getEntryPoint(), fn.getBody().getNumAddresses()));
            } else {
                println("  slot[1] has no Ghidra function");
            }
            byte[] buf = new byte[32];
            try {
                mem.getBytes(slot1Addr, buf);
                println("    first 32 bytes: " + hex(buf, 32));
            } catch (Exception e) { /* ignore */ }
            // Instructions
            if (fn != null) {
                Instruction ins = listing.getInstructionAt(fn.getEntryPoint());
                for (int k = 0; k < 20 && ins != null; k++) {
                    byte[] ib = ins.getBytes();
                    println(String.format("      %s  %-25s  %s", ins.getAddress(), hex(ib, ib.length), ins));
                    boolean isRet = ib.length > 0 && ((ib[0] & 0xFF) == 0xC3 || (ib[0] & 0xFF) == 0xC2);
                    ins = listing.getInstructionAfter(ins.getAddress());
                    if (isRet) break;
                }
                // caller refs
                Reference[] refs = getReferencesTo(slot1Addr);
                println("    references to slot[1] fn: " + refs.length);
                int sh = 0;
                for (Reference r : refs) {
                    if (sh++ >= 6) break;
                    Address fromA = r.getFromAddress();
                    Function caller = getFunctionContaining(fromA);
                    println("      <- " + fromA + "  in " + (caller != null ? caller.getName() : "<no func>"));
                }
                // Decompile slot[1]
                DecompileResults dres = di.decompileFunction(fn, 30, new ConsoleTaskMonitor());
                if (dres != null && dres.decompileCompleted()) {
                    println("    --- slot[1] decompiled ---");
                    String code = dres.getDecompiledFunction().getC();
                    int j = 0;
                    for (String line : code.split("\n")) {
                        println(String.format("      %3d: %s", j++, line));
                    }
                }
            }
        }

        // 4. ALSO: search the whole binary for *.rdata* candidate vtables that happen to be referenced
        //    at offset 0xB60 within their owning class. Skip if step 3 gave us a good answer.

        // 5. Additionally, dump references to the call-site target -- if Ghidra has the class structure
        //    inferred, the dynamic-call target might be in xrefs.
        println("\n--- Direct decompile of FUN_1805616e8 with parameters typed ---");
        Function srcFn = getFunctionAt(sp.getAddress(0x1805616e8L));
        if (srcFn == null) srcFn = getFunctionContaining(sp.getAddress(0x180561756L));
        if (srcFn != null) {
            println("  src func: " + srcFn.getName() + " params=" + srcFn.getParameterCount() + " return=" + srcFn.getReturnType());
        }

        println("\ndone.");
    }
}
