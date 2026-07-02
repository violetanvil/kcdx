"""fs-takeover recon — resolve the IAT targets slot 71's body calls, to confirm role.

slot 71 (0x7ad468) inner FUN_1807ad66c calls [rip+...]→0x3a02840 then tests al & 0x10
(FILE_ATTRIBUTE_DIRECTORY). The string-scan leaves call 0x3a03430 / 0x3a03428.
Resolve those .rdata IAT slots to their imported symbol names to nail the role.
"""
import pefile

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL)
base = pe.OPTIONAL_HEADER.ImageBase

want = {0x3a02840, 0x3a03430, 0x3a03428}
by_va = {}
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for mod in pe.DIRECTORY_ENTRY_IMPORT:
        dll = mod.dll.decode("latin1")
        for imp in mod.imports:
            if imp.address is not None:
                rva = imp.address - base
                nm = imp.name.decode("latin1") if imp.name else f"ord#{imp.ordinal}"
                by_va[rva] = (dll, nm)

for rva in sorted(want):
    if rva in by_va:
        dll, nm = by_va[rva]
        print(f"IAT RVA {rva:#x}  ->  {dll}!{nm}")
    else:
        print(f"IAT RVA {rva:#x}  ->  <not an import thunk; nearest entries:>")
        for r in sorted(by_va):
            if abs(r - rva) <= 0x20:
                print(f"    {r:#x}: {by_va[r][0]}!{by_va[r][1]}")
