"""PROBE G (CRT-noise filter design) — does the PDB expose a per-symbol SOURCE
FILE, so PDB-autoload can keep only the author's own functions and drop the
CRT/compiler plumbing? For the author target + a sample of CRT internals, call
SymGetLineFromAddr64(symbol address) and print the source file DbgHelp reports.

Outcome -> meaning:
  - author fn has a source file under the plugin's own tree, CRT internals have
    a CRT/UCRT/MSVC source path (or none) -> filter by source-file is viable.
  - no source file for ANY (all None) -> SymGetLineFromAddr64 unavailable here;
    fall back to a name-based CRT denylist.

Run: python _research/ki0014-pdb-recon/probe_g_srcfile.py <cap-90.dll>
"""
import ctypes as C
import sys, os
from ctypes import wintypes
sys.path.insert(0, os.path.dirname(__file__))
from probe_d_isolate import (SYMBOL_INFO, ENUM_CB, DWORD64, SYMOPT_UNDNAME,
                             SYMOPT_LOAD_LINES, SYMOPT_DEBUG)

dbghelp = C.WinDLL("dbghelp.dll"); kernel32 = C.WinDLL("kernel32.dll")

class IMAGEHLP_LINE64(C.Structure):
    _fields_ = [("SizeOfStruct", wintypes.DWORD),
                ("Key", C.c_void_p),
                ("LineNumber", wintypes.DWORD),
                ("FileName", C.c_char_p),
                ("Address", DWORD64)]

dbghelp.SymInitialize.argtypes=[wintypes.HANDLE,C.c_char_p,wintypes.BOOL]; dbghelp.SymInitialize.restype=wintypes.BOOL
dbghelp.SymSetOptions.argtypes=[wintypes.DWORD]
dbghelp.SymLoadModuleEx.argtypes=[wintypes.HANDLE,wintypes.HANDLE,C.c_char_p,C.c_char_p,DWORD64,wintypes.DWORD,C.c_void_p,wintypes.DWORD]; dbghelp.SymLoadModuleEx.restype=DWORD64
dbghelp.SymEnumSymbols.argtypes=[wintypes.HANDLE,DWORD64,C.c_char_p,ENUM_CB,C.c_void_p]; dbghelp.SymEnumSymbols.restype=wintypes.BOOL
dbghelp.SymGetLineFromAddr64.argtypes=[wintypes.HANDLE,DWORD64,C.POINTER(wintypes.DWORD),C.POINTER(IMAGEHLP_LINE64)]; dbghelp.SymGetLineFromAddr64.restype=wintypes.BOOL
kernel32.LoadLibraryExW.restype=C.c_void_p; kernel32.LoadLibraryExW.argtypes=[wintypes.LPCWSTR,wintypes.HANDLE,wintypes.DWORD]

SAMPLES = ("cap90_internal_target", "operator delete", "bad_exception",
           "_set_new_handler", "fin$0", "_fltused", "__acrt_lconv_c")

def main(dll):
    proc=kernel32.GetCurrentProcess()
    dbghelp.SymInitialize(proc,None,False)
    dbghelp.SymSetOptions(SYMOPT_UNDNAME|SYMOPT_LOAD_LINES|SYMOPT_DEBUG)
    hmod=kernel32.LoadLibraryExW(dll,None,0x1) or 0
    base=dbghelp.SymLoadModuleEx(proc,None,dll.encode(),None,hmod or 0x40000000,0,None,0)
    found={}
    def cb(psym,sz,ctx):
        s=psym.contents
        nm=s.Name[:s.NameLen].decode(errors="replace")
        for sample in SAMPLES:
            if sample in nm and sample not in found:
                found[sample]=(nm, s.Address)
        return True
    dbghelp.SymEnumSymbols(proc,base,b"*",ENUM_CB(cb),None)
    print(f"loaded base=0x{base:X}")
    for sample in SAMPLES:
        if sample not in found:
            print(f"  {sample:30} (not enumerated)"); continue
        nm, addr = found[sample]
        line=IMAGEHLP_LINE64(); line.SizeOfStruct=C.sizeof(IMAGEHLP_LINE64)
        disp=wintypes.DWORD(0)
        ok=dbghelp.SymGetLineFromAddr64(proc,addr,C.byref(disp),C.byref(line))
        src=(line.FileName.decode(errors="replace") if (ok and line.FileName) else "(no source)")
        print(f"  {nm[:38]:38} -> {src}")

if __name__=="__main__":
    main(sys.argv[1])
