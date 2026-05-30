"""Locate IConsole (CXConsole) vtable base in WHGame.dll .rdata, dump the full
vtable, and identify the PrintLine / Print-family slot EMPIRICALLY.

Method:
  1. Find the vtable base by scanning .rdata for the known-slot pointer
     fingerprint. We have five empirically-verified method RVAs (image-relative):
        slot 23 GetCVar            = 0x009DF818
        slot 32 AddCommand(script) = 0x0100A3D4
        slot 33 AddCommand(func)   = 0x00B9A2B0
        slot 34 RemoveCommand      = 0x0100955C
        slot 35 ExecuteString      = 0x007A5818
     As 8-byte LE virtual addresses (base 0x180000000) they sit at fixed
     relative offsets in the vtable. Scan .rdata qword-aligned for a window
     where vbase+23*8, +32*8, +33*8, +34*8, +35*8 all equal the expected VAs.
     A unique hit = the vtable base. This does NOT trust the canonical header.
  2. Dump slots 0..63 with their target RVAs + section.
  3. For each candidate slot, disassemble the function prologue/body to find
     the one matching PrintLine(const char* s): single pointer arg in rdx,
     appends to a console line buffer (the visible-overlay path), distinct
     from Exit (varargs + abort) and from a log-only writer.

Canonical IConsole.h order (muyuanjin/kcd2db, CryEngine 5.2.3) is used ONLY as
a lead for which slots to inspect — the slot number reported is the binary's.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
IMAGE_BASE = 0x180000000

# Empirically-verified IConsole slots (RVA, image-relative) -> slot index.
KNOWN = {
    23: 0x009DF818,  # GetCVar
    32: 0x0100A3D4,  # AddCommand (script-string overload)
    33: 0x00B9A2B0,  # AddCommand (func-pointer overload)
    34: 0x0100955C,  # RemoveCommand
    35: 0x007A5818,  # ExecuteString
}

def section_of(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode("latin1")
    return "?"

def main():
    pe = pefile.PE(DLL, fast_load=True)
    rdata = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".rdata")
    rdata_bytes = rdata.get_data()
    rdata_va = rdata.VirtualAddress  # RVA of .rdata start

    # Expected absolute VAs for the known slots.
    want = {slot: IMAGE_BASE + rva for slot, rva in KNOWN.items()}
    min_slot = min(KNOWN); max_slot = max(KNOWN)

    # Scan .rdata, qword-aligned, treating each position as a candidate vtable base.
    # For a candidate base at file-offset off (RVA = rdata_va + off), slot k lives
    # at off + k*8.
    hits = []
    n = len(rdata_bytes)
    # base offset must allow reading up to max_slot*8+8
    for off in range(0, n - (max_slot * 8 + 8), 8):
        ok = True
        for slot, wantva in want.items():
            p = off + slot * 8
            val = int.from_bytes(rdata_bytes[p:p + 8], "little")
            if val != wantva:
                ok = False
                break
        if ok:
            hits.append(off)

    print(f"vtable-base candidates: {len(hits)}")
    for off in hits:
        vbase_rva = rdata_va + off
        print(f"\n=== IConsole vtable base @ RVA {vbase_rva:#010x} (VA {IMAGE_BASE+vbase_rva:#x}) section={section_of(pe, vbase_rva)} ===")
        # Dump slots 0..63
        for k in range(0, 64):
            p = off + k * 8
            if p + 8 > n:
                break
            val = int.from_bytes(rdata_bytes[p:p + 8], "little")
            if val == 0:
                tag = "  <null>"
                rva = 0
            else:
                rva = val - IMAGE_BASE
                tag = f"  RVA {rva:#010x}  sect={section_of(pe, rva)}"
            mark = ""
            if k in KNOWN:
                mark = "  <-- KNOWN " + {23:"GetCVar",32:"AddCommand(script)",33:"AddCommand(func)",34:"RemoveCommand",35:"ExecuteString"}[k]
            print(f"  slot[{k:2}]  off=0x{k*8:03x}  VA={val:#x}{tag}{mark}")

    if not hits:
        print("NO vtable base found by the 5-slot fingerprint -- the layout assumption is wrong.")
    return hits, pe, rdata_bytes, rdata_va

if __name__ == "__main__":
    main()
