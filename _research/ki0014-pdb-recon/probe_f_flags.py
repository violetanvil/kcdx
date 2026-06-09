"""PROBE F (KI-0014) — at the REAL mapped base, dump the exact Flags + Tag for the
target function vs the in-range public symbols, to design a code-vs-data
discriminator (the fix must accept the function but reject public DATA).

SYMFLAG bits of interest:
  SYMFLAG_FUNCTION   0x00000800
  SYMFLAG_PUBLIC_CODE 0x02000000  (a PUBLIC symbol marking CODE)
  Tag: SymTagFunction=5, SymTagPublicSymbol=10, SymTagData=7

Run: python _research/ki0014-pdb-recon/probe_f_flags.py <cap-90.dll>
"""
import ctypes as C
import sys, os
from ctypes import wintypes
sys.path.insert(0, os.path.dirname(__file__))
from probe_d_isolate import (SYMBOL_INFO, ENUM_CB, DWORD64, SYMOPT_UNDNAME,
                             SYMOPT_LOAD_LINES, SYMOPT_DEBUG, SYMFLAG_FUNCTION)

dbghelp = C.WinDLL("dbghelp.dll"); kernel32 = C.WinDLL("kernel32.dll")
SYMFLAG_PUBLIC_CODE = 0x02000000
dbghelp.SymInitialize.argtypes=[wintypes.HANDLE,C.c_char_p,wintypes.BOOL]; dbghelp.SymInitialize.restype=wintypes.BOOL
dbghelp.SymSetOptions.argtypes=[wintypes.DWORD]
dbghelp.SymLoadModuleEx.argtypes=[wintypes.HANDLE,wintypes.HANDLE,C.c_char_p,C.c_char_p,DWORD64,wintypes.DWORD,C.c_void_p,wintypes.DWORD]; dbghelp.SymLoadModuleEx.restype=DWORD64
dbghelp.SymEnumSymbols.argtypes=[wintypes.HANDLE,DWORD64,C.c_char_p,ENUM_CB,C.c_void_p]; dbghelp.SymEnumSymbols.restype=wintypes.BOOL
kernel32.LoadLibraryExW.restype=C.c_void_p; kernel32.LoadLibraryExW.argtypes=[wintypes.LPCWSTR,wintypes.HANDLE,wintypes.DWORD]

def main(dll):
    proc=kernel32.GetCurrentProcess()
    dbghelp.SymInitialize(proc,None,False)
    dbghelp.SymSetOptions(SYMOPT_UNDNAME|SYMOPT_LOAD_LINES|SYMOPT_DEBUG)
    hmod=kernel32.LoadLibraryExW(dll,None,0x1) or 0
    base=dbghelp.SymLoadModuleEx(proc,None,dll.encode(),None,hmod or 0x40000000,0,None,0)
    rows=[]
    def cb(psym,sz,ctx):
        s=psym.contents
        nm=s.Name[:s.NameLen].decode(errors="replace")
        # collect the target + a few CRT samples to compare flags/tag
        if ("cap90_internal_target" in nm or "cap89_internal_probe_target" in nm
                or nm in ("__newclmap","_fltused","__acrt_lconv_c","_environ_table")):
            rows.append((nm, s.Flags, s.Tag, s.Address))
        return True
    dbghelp.SymEnumSymbols(proc,base,b"*",ENUM_CB(cb),None)
    print(f"loaded base=0x{base:X}")
    print(f"{'name':40} {'flags':>12} {'PUBLIC_CODE?':>12} {'FUNCTION?':>10} {'tag':>5} addr")
    for nm,fl,tag,addr in rows:
        print(f"{nm:40} {hex(fl):>12} {str(bool(fl&SYMFLAG_PUBLIC_CODE)):>12} "
              f"{str(bool(fl&SYMFLAG_FUNCTION)):>10} {tag:>5} 0x{addr:X}")
    if not rows: print("  (target not found — name mismatch for this DLL)")

if __name__=="__main__":
    main(sys.argv[1])
