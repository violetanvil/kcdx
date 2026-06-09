"""PROBE E (KI-0014) — WHY is NumSyms=0 (private/DBI stream unread) for a matched
FULL PDB? Iterate the load params offline, one variable at a time, watching NumSyms
+ whether cap90_internal_target reads is_func=yes.

Reuses the structs from probe_d_isolate. Each variant prints: variant name,
SymLoadModuleEx base/lasterr, NumSyms, total/funcs, target is_func.

Run: python _research/ki0014-pdb-recon/probe_e_loadparams.py <cap-90.dll>
"""
import ctypes as C
import sys, os
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(__file__))
from probe_d_isolate import (IMAGEHLP_MODULE64, SYMBOL_INFO, ENUM_CB, SYM_TYPE,
                             DWORD64, SYMOPT_UNDNAME, SYMOPT_LOAD_LINES,
                             SYMOPT_DEBUG, SYMFLAG_FUNCTION)

dbghelp = C.WinDLL("dbghelp.dll")
kernel32 = C.WinDLL("kernel32.dll")
SYMOPT_LOAD_ANYTHING = 0x00000040
SYMOPT_DEFERRED_LOADS = 0x00000004
SYMOPT_PUBLICS_ONLY = 0x00004000
SYMOPT_NO_PUBLICS    = 0x00008000

for fn, argt, ret in [
    ("SymInitialize", [wintypes.HANDLE, C.c_char_p, wintypes.BOOL], wintypes.BOOL),
    ("SymCleanup", [wintypes.HANDLE], wintypes.BOOL),
    ("SymSetOptions", [wintypes.DWORD], wintypes.DWORD),
    ("SymLoadModuleEx", [wintypes.HANDLE, wintypes.HANDLE, C.c_char_p, C.c_char_p,
                         DWORD64, wintypes.DWORD, C.c_void_p, wintypes.DWORD], DWORD64),
    ("SymGetModuleInfo64", [wintypes.HANDLE, DWORD64, C.POINTER(IMAGEHLP_MODULE64)], wintypes.BOOL),
    ("SymEnumSymbols", [wintypes.HANDLE, DWORD64, C.c_char_p, ENUM_CB, C.c_void_p], wintypes.BOOL),
    ("SymUnloadModule64", [wintypes.HANDLE, DWORD64], wintypes.BOOL),
]:
    getattr(dbghelp, fn).argtypes = argt
    getattr(dbghelp, fn).restype = ret

def run_variant(name, dll, opts, use_real_image):
    proc = kernel32.GetCurrentProcess()
    dbghelp.SymInitialize(proc, None, False)
    dbghelp.SymSetOptions(opts)
    hmod = 0
    base_arg, size_arg = 0x30000000, 0
    if use_real_image:
        kernel32.LoadLibraryExW.restype = C.c_void_p
        kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
        kernel32.FreeLibrary.argtypes = [C.c_void_p]
        DONT_RESOLVE_DLL_REFERENCES = 0x1
        hmod = kernel32.LoadLibraryExW(dll, None, DONT_RESOLVE_DLL_REFERENCES) or 0
        if hmod:
            base_arg = hmod  # the real mapped base
            size_arg = 0     # size 0 lets DbgHelp size from the mapped image
    loaded = dbghelp.SymLoadModuleEx(proc, None, dll.encode(), None, base_arg, size_arg, None, 0)
    le = kernel32.GetLastError()
    res = {"name": name, "loaded": loaded, "lasterr": le, "numsyms": None,
           "symtype": None, "total": 0, "funcs": 0, "target_isfunc": None, "target_flags": None}
    if loaded:
        im = IMAGEHLP_MODULE64(); im.SizeOfStruct = C.sizeof(IMAGEHLP_MODULE64)
        if dbghelp.SymGetModuleInfo64(proc, loaded, C.byref(im)):
            res["numsyms"] = im.NumSyms; res["symtype"] = im.SymType
        stats = {"t": 0, "f": 0, "tf": None, "tflags": None}
        def cb(psym, sz, ctx):
            s = psym.contents; stats["t"] += 1
            if s.Flags & SYMFLAG_FUNCTION: stats["f"] += 1
            nm = s.Name[:s.NameLen].decode(errors="replace")
            if "cap90_internal_target" in nm:
                stats["tf"] = bool(s.Flags & SYMFLAG_FUNCTION); stats["tflags"] = s.Flags
            return True
        dbghelp.SymEnumSymbols(proc, loaded, b"*", ENUM_CB(cb), None)
        res.update(total=stats["t"], funcs=stats["f"],
                   target_isfunc=stats["tf"], target_flags=stats["tflags"])
        dbghelp.SymUnloadModule64(proc, loaded)
    dbghelp.SymCleanup(proc)
    if hmod: kernel32.FreeLibrary(hmod)
    print(f"[{name}] loaded=0x{loaded:X} err={res['lasterr']} symtype={res['symtype']}"
          f"({SYM_TYPE.get(res['symtype'],'?')}) NumSyms={res['numsyms']} "
          f"total={res['total']} funcs={res['funcs']} "
          f"target_isfunc={res['target_isfunc']} target_flags="
          f"{hex(res['target_flags']) if res['target_flags'] is not None else None}")

def main(dll):
    base = SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEBUG
    run_variant("baseline (fake base, size0)", dll, base, use_real_image=False)
    run_variant("LOAD_ANYTHING", dll, base | SYMOPT_LOAD_ANYTHING, use_real_image=False)
    run_variant("NO_PUBLICS (force private)", dll, base | SYMOPT_NO_PUBLICS, use_real_image=False)
    run_variant("real-mapped image base", dll, base, use_real_image=True)
    run_variant("real-mapped + LOAD_ANYTHING", dll, base | SYMOPT_LOAD_ANYTHING, use_real_image=True)
    run_variant("real-mapped + NO_PUBLICS", dll, base | SYMOPT_NO_PUBLICS, use_real_image=True)

if __name__ == "__main__":
    if len(sys.argv) < 2: print("usage: probe_e_loadparams.py <cap-90.dll>"); sys.exit(1)
    main(sys.argv[1])
