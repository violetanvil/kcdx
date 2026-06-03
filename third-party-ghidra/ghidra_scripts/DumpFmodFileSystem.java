// F4 (audio) recon — read the FMOD file-system integration in WHGame.dll.
// Question: does audio file I/O reach CCryPak::FOpen (slot 36, 0x4614A0) + FRead
// (FGetCachedFileData 0x51CD00), or bypass CCryPak via Win32 CreateFile / FMOD's own fopen?
// setFileSystem is imported + called ONCE (call site RVA 0xd30040). The function CONTAINING
// that call registers FMOD's open/read/seek/close callbacks; the open callback's body is the
// answer. Also decompile the bank/stream loaders in the same 0xd30xxx FMOD-core neighborhood.
// Image base 0x180000000.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpFmodFileSystem extends GhidraScript {

    void dumpFunc(long fa, String label, DecompInterface di, AddressSpace sp) throws Exception {
        Address a = sp.getAddress(fa);
        Function f = getFunctionAt(a);
        if (f == null) f = getFunctionContaining(a);
        println("\n================================================================================");
        println(label + " @ " + a);
        println("================================================================================");
        if (f == null) { println("  NO FUNCTION at this address"); return; }
        println("  entry: " + f.getEntryPoint() + "   proto: " + f.getPrototypeString(true, false));
        println("  size (addrs): " + f.getBody().getNumAddresses());
        println("  --- decompiled C ---");
        DecompileResults dr = di.decompileFunction(f, 180, new ConsoleTaskMonitor());
        if (dr != null && dr.decompileCompleted()) {
            println(dr.getDecompiledFunction().getC());
        } else {
            println("  (decompile failed: " + (dr != null ? dr.getErrorMessage() : "null") + ")");
        }
    }

    // raw-disassemble a window of instructions ending at `callRva` to see the LEA/MOV that
    // load the callback function pointers passed to setFileSystem (the args before the call).
    void dumpAsmWindow(long startRva, long endRva, String label, AddressSpace sp) throws Exception {
        println("\n---- ASM window: " + label + " ----");
        Address a = sp.getAddress(0x180000000L + startRva);
        Address end = sp.getAddress(0x180000000L + endRva);
        Instruction ins = getInstructionAt(a);
        if (ins == null) ins = getInstructionAfter(a);
        int guard = 0;
        while (ins != null && ins.getAddress().compareTo(end) <= 0 && guard++ < 400) {
            println(String.format("  %s  %s", ins.getAddress(), ins.toString()));
            ins = ins.getNext();
        }
    }

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // The lone setFileSystem call site — its containing function registers the callbacks.
        dumpFunc(0x180d30040L, "FMOD setFileSystem caller (registers FS callbacks) @ call-site 0xD30040", di, sp);
        // ASM just before the call to read the callback-pointer LEAs (rdx/r8/r9/stack args).
        dumpAsmWindow(0xd2ffc0L, 0xd30060L, "around setFileSystem call 0xD30040 (callback LEAs)", sp);

        // FMOD-core neighborhood loaders (same 0xd30xxx cluster):
        dumpFunc(0x180d301eeL, "loadBankFile caller @ 0xD301EE", di, sp);
        dumpFunc(0x180d3035bL, "createSound caller @ 0xD3035B", di, sp);
        dumpFunc(0x180b473d8L, "loadBankFile caller @ 0xB473D8 (separate site)", di, sp);

        println("\ndone.");
    }
}
