// KI-0028 differential trace — Step 1: name the render-SUBMISSION edges between
// "geometry created" and IASetIndexBuffer, so the ordered tracer instruments REAL
// adjacent engine edges, not guesses. See _research/ki0028-differential-trace-recon/DESIGN.md.
//
// Discovery model (same as FindShaderManAnchors): CryEngine render source-path + method
// strings -> LEA/xref site -> the function it pins -> caller/callee graph. The draw-record
// path in a CryEngine D3D12 wrapper is: RenderView -> SceneRenderPass -> CompiledRenderObject
// draw-packet submit -> CDeviceGraphicsCommandInterface::{SetIndexBuffer,DrawIndexed} ->
// the D3D12 command-list COM call (slot 43 IASetIndexBuffer / slot 13 DrawIndexedInstanced,
// the drawcall_probe boundary). We anchor on the ENGINE-side strings and emit, per anchor:
//   - the pinned function (entry) + its source string
//   - CALLERS (who reaches this submission fn — the upstream trace edge)
//   - CALLEES that look like the next step toward the command list
// Read-only. Output feeds the tracer's instrument-set + the offline diff.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class Ki28RenderSubmitAnchors extends GhidraScript {

    // Ghidra's headless logger collapses multi-line println output — write to a file.
    static final String OUT =
        "C:\\Users\\Michael\\Documents\\KCD2 Mods\\kcdx\\_research\\"
        + "ki0028-differential-trace-recon\\_render_submit_edges.txt";
    PrintWriter w;
    void emit(String s) { w.println(s); }

    // CryEngine render-SUBMISSION source-path + class/method anchors. Precise tokens only —
    // the first pass showed loose substrings (start_/sort_/cart_/RT_/body_part) flood with
    // gameplay-data false positives. These are render-DLL class/method/source-file strings.
    static final String[] NEEDLES = {
        // --- render-item / render-object / draw-packet build+submit ---
        "ccompiledrenderobject", "compiledrenderobject", "crenderobject",
        "crenderview::", "renderview.cpp", "addpolygon", "sortrenderitems",
        "drawcompiledrenderobject", "drawinstances",
        // --- scene render passes (the per-pass draw loop) ---
        "scenerenderpass", "csceneforwardpass", "cscenegbufferpass",
        "graphicspipeline.cpp", "standardgraphicspipeline",
        // --- the device command-list wrapper (the leaf just above D3D12) ---
        "cdevicegraphicscommandinterface", "devicecommandlistcommon_d3d12",
        "drawindexedinstanced", "commandlistpool", "cdeviceobjectfactory",
        "cdevicecommandlist",
        // --- render mesh (the geometry -> IB/VB binding source) ---
        "rendermesh.cpp", "crendermesh", "setmesh_impl", "bindstreams",
        // --- the render thread / flush (top of submit) ---
        "renderthread.cpp", "cryrenderd3d12", "ccrydx12", "executecommandlists"
    };

    boolean matches(String s) {
        String l = s.toLowerCase();
        for (String n : NEEDLES) if (l.contains(n)) return true;
        return false;
    }

    long base;

    String off(Address a) { return "0x" + Long.toHexString(a.getOffset() - base); }

    // Callers of f: xref-to entry, mapped to containing function.
    Set<String> callersOf(Function f) {
        Set<String> out = new LinkedHashSet<>();
        ReferenceManager rm = currentProgram.getReferenceManager();
        ReferenceIterator refs = rm.getReferencesTo(f.getEntryPoint());
        int n = 0;
        while (refs.hasNext() && n < 16) {
            Reference r = refs.next();
            if (!r.getReferenceType().isCall()) continue;
            Function c = getFunctionContaining(r.getFromAddress());
            if (c == null) continue;
            out.add(c.getName() + " entry=" + off(c.getEntryPoint())
                    + " callsite=" + off(r.getFromAddress()));
            n++;
        }
        return out;
    }

    // Direct callees (call FUN_xxx) inside f — the next step toward the command list.
    Set<String> calleesOf(Function f) {
        Set<String> out = new LinkedHashSet<>();
        Address a = f.getEntryPoint();
        Address end = f.getBody().getMaxAddress();
        InstructionIterator it = currentProgram.getListing().getInstructions(a, true);
        int n = 0;
        while (it.hasNext() && n < 40) {
            Instruction insn = it.next();
            if (insn.getAddress().compareTo(end) > 0) break;
            String m = insn.getMnemonicString();
            if (!m.equals("call")) continue;
            Reference[] rs = insn.getReferencesFrom();
            for (Reference r : rs) {
                if (!r.getReferenceType().isCall()) continue;
                Function callee = getFunctionAt(r.getToAddress());
                String tgt = (callee != null)
                        ? callee.getName() + "@" + off(callee.getEntryPoint())
                        : "indirect@" + off(insn.getAddress());
                out.add("callsite=" + off(insn.getAddress()) + " -> " + tgt);
                n++;
            }
        }
        return out;
    }

    @Override
    public void run() throws Exception {
        base = currentProgram.getImageBase().getOffset();
        w = new PrintWriter(OUT);
        try {
            ReferenceManager rm = currentProgram.getReferenceManager();
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            int hits = 0;
            Set<Address> seenFns = new LinkedHashSet<>();

            emit("=== KI-0028 RENDER-SUBMISSION anchors (image base 0x"
                    + Long.toHexString(base) + "; all addresses below are RVA = VA-base) ===");
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                if (d == null) continue;
                String type = d.getDataType().getName().toLowerCase();
                if (!type.contains("string") && !type.contains("char")) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                if (s.length() < 4 || s.length() > 200) continue;
                if (!matches(s)) continue;

                Address sa = d.getAddress();
                ReferenceIterator refs = rm.getReferencesTo(sa);
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f == null) continue;
                    if (!seenFns.add(f.getEntryPoint())) continue;

                    hits++;
                    emit("");
                    emit("ANCHOR \"" + s.replace("\n", "\\n") + "\"");
                    emit("  PINS fn " + f.getName() + " entry=" + off(f.getEntryPoint())
                            + " (xref-site=" + off(r.getFromAddress()) + ")");
                    Set<String> callers = callersOf(f);
                    emit("  CALLERS (" + callers.size() + " — upstream trace edge):");
                    for (String c : callers) emit("    <- " + c);
                    Set<String> callees = calleesOf(f);
                    emit("  CALLEES (toward the command list):");
                    for (String c : callees) emit("    -> " + c);
                }
            }
            emit("");
            emit("=== " + hits + " render-submission functions pinned ===");
            println("Ki28RenderSubmitAnchors: " + hits + " functions -> " + OUT);
        } finally {
            w.flush();
            w.close();
        }
    }
}
