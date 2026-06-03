// F4 (audio) recon — decompile the FOUR FMOD file-system callbacks WHGame registers via
// FMOD::System::setFileSystem(open,close,read,seek,...) at FUN_180d2fde4 (FmodWrapper.cpp).
// These callbacks ARE FMOD's file I/O — every .bank / .ogg FMOD opens goes through them.
// Question (AP19 — read the body, don't infer from address proximity): does the OPEN callback
// call CCryPak::FOpen (slot 36 via gEnv->pCryPak, vtable+0x120), and does READ reach FRead/
// FGetCachedFileData (0x51CD00)? Or do they call Win32 CreateFile / CRT fopen directly?
// Registered pointers (from the setFileSystem call decompile):
//   useropen  = FUN_181224d1c
//   userclose = FUN_1813437b4
//   userread  = FUN_180460788   (in the CCryPak FOpen/FClose 0x460xxx cluster)
//   userseek  = FUN_1810b6d84
// Image base 0x180000000.
//@category KCD2

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;

public class DumpFmodCallbacks extends GhidraScript {

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

    @Override
    public void run() throws Exception {
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        dumpFunc(0x181224d1cL, "FMOD useropen  callback FUN_181224D1C  (does it call CCryPak::FOpen slot36 0x4614A0?)", di, sp);
        dumpFunc(0x180460788L, "FMOD userread  callback FUN_180460788  (does it reach FRead/FGetCachedFileData 0x51CD00?)", di, sp);
        dumpFunc(0x1810b6d84L, "FMOD userseek  callback FUN_1810B6D84", di, sp);
        dumpFunc(0x1813437b4L, "FMOD userclose callback FUN_1813437B4", di, sp);

        println("\ndone.");
    }
}
