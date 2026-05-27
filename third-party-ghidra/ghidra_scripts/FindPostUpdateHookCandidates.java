// Find functions that fire on the main thread AFTER first-update-tick
// but BEFORE the main menu is reached — without player input.
//
// Strategy:
//  1. Locate CGame::Update (the function the kcdx hook is on) by its known
//     prologue sig: 48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55
//                   41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ?
//     Then walk every directly-called function and rank by frequency.
//  2. Hunt for CryEngine landmark strings in the binary:
//       "OnSystemStarted", "PostUpdate", "PreUpdate", "FlowSystem",
//       "MainMenu", "CGameFramework", "CCryAction", "ESYSTEM_EVENT",
//       "FrameProfiler", "CScriptSystem", "ScriptBind".
//     For each landmark, walk back to the containing function and print
//     its entry, plus a 24-byte AOB pattern.
//  3. For each candidate, write entry AOB (first 24 bytes with any
//     RIP-relative immediates wildcarded).
//
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.RefType;
import ghidra.program.util.DefinedDataIterator;
import ghidra.program.model.data.StringDataInstance;

import java.util.*;

public class FindPostUpdateHookCandidates extends GhidraScript {

    @Override
    protected void run() throws Exception {
        println("============================================================");
        println("Post-Update Main-Thread Hook Candidate Hunter");
        println("============================================================");

        Program program = currentProgram;
        Memory mem = program.getMemory();
        Listing listing = program.getListing();
        FunctionManager fm = program.getFunctionManager();
        ReferenceManager rm = program.getReferenceManager();

        // ---- Stage 1: find CGame::Update via its known prologue sig ----
        // The actual byte pattern (no spaces, ? = wildcard):
        // 488BC44889587?4889707?4889787?554154415541564157488DA8????????4881EC????????
        byte[] patBytes = parsePattern(
            "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57");
        boolean[] patMask = parseMask(
            "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57");

        Address updateAddr = findUniqueInExecutable(mem, patBytes, patMask);
        if (updateAddr == null) {
            println("[WARN] Could not uniquely locate CGame::Update via sig — falling back to landmark-only mode.");
        } else {
            println("[INFO] CGame::Update found at " + updateAddr);
        }

        // ---- Stage 2: walk direct calls FROM CGame::Update ----
        if (updateAddr != null) {
            Function updateFn = fm.getFunctionContaining(updateAddr);
            if (updateFn == null) {
                println("[WARN] No function containing " + updateAddr + "; making one");
                updateFn = createFunction(updateAddr, "CGame_Update_guess");
            }
            if (updateFn != null) {
                println("[INFO] Containing function: " + updateFn.getName()
                        + " @ " + updateFn.getEntryPoint()
                        + " (body " + updateFn.getBody().getNumAddresses() + " bytes)");
                Map<Address, Integer> callees = new LinkedHashMap<>();
                InstructionIterator ii = listing.getInstructions(updateFn.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    if (!ins.getMnemonicString().startsWith("CALL")) continue;
                    for (Reference r : ins.getReferencesFrom()) {
                        if (!r.getReferenceType().isCall()) continue;
                        Address to = r.getToAddress();
                        callees.merge(to, 1, Integer::sum);
                    }
                }
                println("[INFO] Direct callees from CGame::Update: " + callees.size());
                int rank = 0;
                for (Map.Entry<Address, Integer> e : callees.entrySet()) {
                    if (++rank > 40) break;
                    Function callee = fm.getFunctionAt(e.getKey());
                    String name = (callee != null) ? callee.getName() : "(no func at addr)";
                    println(String.format("  [callee %2d] %s @ %s  count=%d",
                            rank, name, e.getKey(), e.getValue()));
                    if (callee != null) {
                        byte[] proto = readBytes(mem, callee.getEntryPoint(), 24);
                        if (proto != null) {
                            String sig = aobOf(proto);
                            println("              entry-AOB(24): " + sig);
                        }
                    }
                }
            }
        }

        // ---- Stage 3: hunt CryEngine landmark strings ----
        String[] landmarks = {
            "OnSystemStarted",
            "PostUpdate",
            "PreUpdate",
            "CGameFramework",
            "CCryAction",
            "FlowSystem",
            "CFrameProfile",
            "CScriptSystem",
            "ScriptBind_",
            "MainMenu",
            "ESYSTEM_EVENT_LEVEL",
            "ESYSTEM_EVENT_GAME_POST_INIT",
            "ESYSTEM_EVENT_FRONTEND_INITIALISED",
            "CCryActionGame",
            "OnFrameStart",
            "OnPostUpdate",
            "OnPreRender"
        };

        println("\n[INFO] String landmark sweep");
        Set<Address> dumped = new LinkedHashSet<>();
        DefinedDataIterator strs = DefinedDataIterator.definedStrings(program);
        int found = 0;
        while (strs.hasNext()) {
            ghidra.program.model.listing.Data d = strs.next();
            String s = null;
            try {
                Object v = d.getValue();
                if (v != null) s = v.toString();
            } catch (Throwable t) { continue; }
            if (s == null) continue;
            String hit = null;
            for (String lm : landmarks) {
                if (s.contains(lm)) { hit = lm; break; }
            }
            if (hit == null) continue;
            found++;
            Address strAddr = d.getMinAddress();

            // find xrefs TO this string
            Reference[] refs = rm.getReferencesTo(strAddr);
            if (refs.length == 0) continue;
            for (Reference r : refs) {
                Address from = r.getFromAddress();
                Function host = fm.getFunctionContaining(from);
                if (host == null) continue;
                Address ep = host.getEntryPoint();
                if (dumped.contains(ep)) continue;
                dumped.add(ep);
                byte[] proto = readBytes(mem, ep, 24);
                String sig = (proto != null) ? aobOf(proto) : "(unreadable)";
                println(String.format(
                    "  landmark=\"%s\" str@%s xref-from %s in %s @%s",
                    hit, strAddr, from, host.getName(), ep));
                println("    snippet: \"" + abbreviate(s, 100) + "\"");
                println("    entry-AOB(24): " + sig);
            }
        }
        println("[INFO] Total landmark strings matched: " + found);
        println("[INFO] Unique landmark-host entries dumped: " + dumped.size());

        // ---- Stage 4: post-process — for each candidate, count xrefs and report uniqueness ----
        println("\n[INFO] AOB uniqueness check for landmark-host entries");
        int idx = 0;
        for (Address ep : dumped) {
            idx++;
            if (idx > 60) {
                println("  (truncated at 60)");
                break;
            }
            byte[] proto = readBytes(mem, ep, 24);
            if (proto == null) continue;
            String sig = aobOf(proto);
            boolean[] mask = new boolean[proto.length];
            Arrays.fill(mask, true);
            int hits = countMatches(mem, proto, mask);
            Function f = fm.getFunctionAt(ep);
            String fname = (f != null) ? f.getName() : "(?)";
            println(String.format("  cand %s @ %s  matches=%d  sig=%s",
                fname, ep, hits, sig));
        }

        println("\n[done]");
    }

    // ---------- helpers ----------

    private static byte[] parsePattern(String s) {
        List<Byte> out = new ArrayList<>();
        String[] toks = s.trim().split("\\s+");
        for (String t : toks) {
            if (t.equals("?") || t.equals("??")) out.add((byte) 0);
            else out.add((byte) Integer.parseInt(t, 16));
        }
        byte[] arr = new byte[out.size()];
        for (int i = 0; i < arr.length; i++) arr[i] = out.get(i);
        return arr;
    }

    private static boolean[] parseMask(String s) {
        List<Boolean> out = new ArrayList<>();
        String[] toks = s.trim().split("\\s+");
        for (String t : toks) out.add(!(t.equals("?") || t.equals("??")));
        boolean[] arr = new boolean[out.size()];
        for (int i = 0; i < arr.length; i++) arr[i] = out.get(i);
        return arr;
    }

    private Address findUniqueInExecutable(Memory mem, byte[] pat, boolean[] mask) {
        Address found = null;
        int count = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute()) continue;
            Address start = blk.getStart();
            byte[] buf;
            try { buf = new byte[(int) Math.min(blk.getSize(), Integer.MAX_VALUE)];
                  mem.getBytes(start, buf);
            } catch (Throwable t) { continue; }
            for (int i = 0; i + pat.length < buf.length; i++) {
                boolean ok = true;
                for (int j = 0; j < pat.length; j++) {
                    if (mask[j] && buf[i + j] != pat[j]) { ok = false; break; }
                }
                if (ok) {
                    count++;
                    if (found == null) found = start.add(i);
                    if (count > 1) break;
                }
            }
            if (count > 1) break;
        }
        if (count == 1) return found;
        println("[WARN] pattern matched " + count + " times across executable sections");
        return (count == 1) ? found : null;
    }

    private int countMatches(Memory mem, byte[] pat, boolean[] mask) {
        int total = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute()) continue;
            byte[] buf;
            try { buf = new byte[(int) Math.min(blk.getSize(), Integer.MAX_VALUE)];
                  mem.getBytes(blk.getStart(), buf);
            } catch (Throwable t) { continue; }
            for (int i = 0; i + pat.length < buf.length; i++) {
                boolean ok = true;
                for (int j = 0; j < pat.length; j++) {
                    if (mask[j] && buf[i + j] != pat[j]) { ok = false; break; }
                }
                if (ok) total++;
            }
        }
        return total;
    }

    private byte[] readBytes(Memory mem, Address a, int len) {
        byte[] b = new byte[len];
        try { mem.getBytes(a, b); } catch (Throwable t) { return null; }
        return b;
    }

    private static String aobOf(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < b.length; i++) {
            if (i > 0) sb.append(' ');
            sb.append(String.format("%02X", b[i] & 0xFF));
        }
        return sb.toString();
    }

    private static String abbreviate(String s, int max) {
        if (s == null) return "";
        s = s.replace("\n", " ").replace("\r", " ");
        return s.length() > max ? s.substring(0, max) + "..." : s;
    }
}
