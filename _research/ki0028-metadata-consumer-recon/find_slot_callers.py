"""KI-0028 DIVERGENCE-A consumer hunt — find DIRECT callers of the metadata slots
67 (IsFileExist3, RVA 0x463ec4), 70 (IsFileExist2, RVA 0x241abcc),
45 (GetFileSize, RVA 0x2418b48), and report the enclosing function start for each
so the next pass reads the consumer body. Direct `call rel32` (E8) only — these are
unambiguous AP19-clean call edges (no false-positive offset noise).

Also scans for the gEnv->pCryPak global load shape to seed a virtual-site pass if
direct callers are sparse.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress
text_start = base + tva
text_end = text_start + len(data)
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

# RVA -> VA
TARGETS = {
    "slot67_IsFileExist3": 0x463ec4,
    "slot70_IsFileExist2": 0x241abcc,
    "slot45_GetFileSize":  0x2418b48,
}
TARGET_VAS = {base + rva: name for name, rva in TARGETS.items()}

# Collect all function-start candidates: a VA that is the target of a direct E8 call
# anywhere (= a callable). We approximate "enclosing function start" as the nearest
# preceding INT3-padding boundary / standard prologue.
def find_direct_callers(target_va):
    hits = []
    for i in range(len(data) - 5):
        if data[i] != 0xE8:
            continue
        rel = int.from_bytes(data[i+1:i+5], "little", signed=True)
        site_va = text_start + i
        dest = site_va + 5 + rel
        if dest == target_va:
            hits.append(site_va)
    return hits

# crude enclosing-function-start: walk back to the byte after a 0xCC pad run or a
# RET (C3) followed by INT3, i.e. the previous function boundary.
def enclosing_func_start(site_va):
    off = site_va - text_start
    j = off
    # walk back up to 4KB for a boundary: CC CC ... then first non-CC = func start
    limit = max(0, off - 0x2000)
    while j > limit:
        if data[j] == 0xCC and j+1 <= off and data[j+1] != 0xCC:
            return text_start + j + 1
        j -= 1
    return None

print("=== DIRECT callers (E8 rel32) of metadata slots ===\n")
for tva_va, name in TARGET_VAS.items():
    callers = find_direct_callers(tva_va)
    print(f"--- {name}  (VA {tva_va:#x} / RVA {tva_va-base:#x}) : {len(callers)} direct caller(s)")
    for c in callers:
        fs = enclosing_func_start(c)
        fs_s = f"func~{fs:#x} (RVA {fs-base:#x})" if fs else "func~?"
        print(f"    call-site VA {c:#x}  RVA {c-base:#x}   in {fs_s}")
    print()
