"""PROBE D (KI-0014) — does cap-90.pdb enumerate its private function in ISOLATION?

Offline DbgHelp via ctypes — one module, fresh handler, no game, no other
plugin loaded first. Splits the last fork:
  - target enumerates here (is_func, in range) → the FILE is innocent; the
    in-game zero-funcs is ENVIRONMENTAL (the load loop / Nth-module state).
  - num_syms=0 / target absent here too → the file or the load PARAMS are the
    cause (reproduces outside the loop), not the loop environment.

Run:  python _research/ki0014-pdb-recon/probe_d_isolate.py <path-to-cap-90.dll>
The .pdb must sit beside the DLL (or be findable via the DLL's embedded path).
"""
import ctypes as C
import sys
from ctypes import wintypes

dbghelp = C.WinDLL("dbghelp.dll")
kernel32 = C.WinDLL("kernel32.dll")

DWORD64 = C.c_uint64
SYMOPT_UNDNAME = 0x00000002
SYMOPT_LOAD_LINES = 0x00000010
SYMOPT_DEBUG = 0x80000000
SYMFLAG_FUNCTION = 0x00000800

CBA_NONE = 0

# IMAGEHLP_MODULE64 — we read SymType, LoadedPdbName, PdbUnmatched, NumSyms.
MAX_PATH = 260
class IMAGEHLP_MODULE64(C.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.DWORD),
        ("BaseOfImage", DWORD64),
        ("ImageSize", wintypes.DWORD),
        ("TimeDateStamp", wintypes.DWORD),
        ("CheckSum", wintypes.DWORD),
        ("NumSyms", wintypes.DWORD),
        ("SymType", wintypes.DWORD),
        ("ModuleName", C.c_char * 32),
        ("ImageName", C.c_char * 256),
        ("LoadedImageName", C.c_char * 256),
        ("LoadedPdbName", C.c_char * 256),
        ("CVSig", wintypes.DWORD),
        ("CVData", C.c_char * (MAX_PATH * 3)),
        ("PdbSig", wintypes.DWORD),
        ("PdbSig70", C.c_byte * 16),
        ("PdbAge", wintypes.DWORD),
        ("PdbUnmatched", wintypes.BOOL),
        ("DbgUnmatched", wintypes.BOOL),
        ("LineNumbers", wintypes.BOOL),
        ("GlobalSymbols", wintypes.BOOL),
        ("TypeInfo", wintypes.BOOL),
        ("SourceIndexed", wintypes.BOOL),
        ("Publics", wintypes.BOOL),
        ("MachineType", wintypes.DWORD),
        ("Reserved", wintypes.DWORD),
    ]

SYM_TYPE = {0: "SymNone", 1: "SymCoff", 2: "SymCv", 3: "SymPdb",
            4: "SymExport", 5: "SymDeferred", 6: "SymSym", 7: "SymDia",
            8: "SymVirtual"}

class SYMBOL_INFO(C.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.ULONG),
        ("TypeIndex", wintypes.ULONG),
        ("Reserved", DWORD64 * 2),
        ("Index", wintypes.ULONG),
        ("Size", wintypes.ULONG),
        ("ModBase", DWORD64),
        ("Flags", wintypes.ULONG),
        ("Value", DWORD64),
        ("Address", DWORD64),
        ("Register", wintypes.ULONG),
        ("Scope", wintypes.ULONG),
        ("Tag", wintypes.ULONG),
        ("NameLen", wintypes.ULONG),
        ("MaxNameLen", wintypes.ULONG),
        ("Name", C.c_char * 1024),
    ]

ENUM_CB = C.WINFUNCTYPE(wintypes.BOOL, C.POINTER(SYMBOL_INFO), wintypes.ULONG, C.c_void_p)

def main(dll_path):
    proc = kernel32.GetCurrentProcess()
    dbghelp.SymInitialize.argtypes = [wintypes.HANDLE, C.c_char_p, wintypes.BOOL]
    dbghelp.SymInitialize.restype = wintypes.BOOL
    dbghelp.SymSetOptions.argtypes = [wintypes.DWORD]
    dbghelp.SymSetOptions.restype = wintypes.DWORD
    dbghelp.SymLoadModuleExW = dbghelp.SymLoadModuleExW  # use ANSI below
    dbghelp.SymLoadModuleEx.argtypes = [wintypes.HANDLE, wintypes.HANDLE, C.c_char_p,
                                        C.c_char_p, DWORD64, wintypes.DWORD,
                                        C.c_void_p, wintypes.DWORD]
    dbghelp.SymLoadModuleEx.restype = DWORD64
    dbghelp.SymGetModuleInfo64.argtypes = [wintypes.HANDLE, DWORD64,
                                           C.POINTER(IMAGEHLP_MODULE64)]
    dbghelp.SymGetModuleInfo64.restype = wintypes.BOOL
    dbghelp.SymEnumSymbols.argtypes = [wintypes.HANDLE, DWORD64, C.c_char_p,
                                       ENUM_CB, C.c_void_p]
    dbghelp.SymEnumSymbols.restype = wintypes.BOOL

    if not dbghelp.SymInitialize(proc, None, False):
        print(f"SymInitialize failed: {kernel32.GetLastError()}"); return
    dbghelp.SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEBUG)

    base = 0x10000000  # arbitrary fake base; size 0 lets DbgHelp size it
    loaded = dbghelp.SymLoadModuleEx(proc, None, dll_path.encode(), None, base, 0, None, 0)
    print(f"SymLoadModuleEx -> base=0x{loaded:X}  lasterr={kernel32.GetLastError()}")
    if loaded == 0:
        print("  load failed / already loaded"); return
    base = loaded

    im = IMAGEHLP_MODULE64()
    im.SizeOfStruct = C.sizeof(IMAGEHLP_MODULE64)
    if dbghelp.SymGetModuleInfo64(proc, base, C.byref(im)):
        print(f"  SymType={im.SymType} ({SYM_TYPE.get(im.SymType,'?')})  "
              f"NumSyms={im.NumSyms}  PdbUnmatched={im.PdbUnmatched}  "
              f"TypeInfo={im.TypeInfo}  GlobalSymbols={im.GlobalSymbols}")
        print(f"  LoadedPdb={im.LoadedPdbName.decode(errors='replace')}")
    else:
        print(f"  SymGetModuleInfo64 failed: {kernel32.GetLastError()}")

    stats = {"total": 0, "funcs": 0, "target": False, "target_addr": 0}
    def cb(psym, size, ctx):
        s = psym.contents
        stats["total"] += 1
        name = s.Name[:s.NameLen].decode(errors="replace")
        if s.Flags & SYMFLAG_FUNCTION:
            stats["funcs"] += 1
        if "cap90_internal_target" in name:
            stats["target"] = True
            stats["target_addr"] = s.Address
        return True
    c_cb = ENUM_CB(cb)
    ok = dbghelp.SymEnumSymbols(proc, base, b"*", c_cb, None)
    print(f"SymEnumSymbols ok={bool(ok)}  total={stats['total']}  funcs={stats['funcs']}")
    print(f"  cap90_internal_target enumerated: {stats['target']}  addr=0x{stats['target_addr']:X}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: probe_d_isolate.py <cap-90.dll>"); sys.exit(1)
    main(sys.argv[1])
