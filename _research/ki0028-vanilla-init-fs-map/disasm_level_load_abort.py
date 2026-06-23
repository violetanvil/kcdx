"""KI-0028 level-load-abort (0xD2) raise-site static recon — APEX FRONT.

Dump kcdx_2026-06-22_18-21-45.dmp settled: main thread in RaiseException, rdi=0xD2,
exception code 000000d2. WHGame base 0x7ff831e90000. Frame return RVAs (derived):
  raise-site (call RaiseException just before)  = 0x23acaca
  caller chain: 0x66ba9c 0x66b6e0 0x669ad5 0x669779 0x669360 0x17ae371
                0xb95f2e 0xb74ebf 0xb96b38 0x7a6131 0x7a5bc4 0x7a5856
                0xfbedb9 0x532f69 0x869c39  -> KingdomCome.exe main

This reads the BINARY (no launch), theory-independent, per AP19 (every call-edge
read from the body). Reuses the pefile+capstone pattern from ki0026-ngx-raise-site-recon.

Q1. The raise-site fn (containing 0x23acaca): confirm it is RaiseException(0xD2);
    read the branch/test that REACHES the raise. What value/flag is tested?
Q2. The IMMEDIATE caller(s) up the chain: what FS/resolve/mount/enumeration/handle
    RESULT feeds the tested condition? (the level-load gate's actual input)
"""
import sys, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
base = pe.OPTIONAL_HEADER.ImageBase  # 0x180000000
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); tva = text.VirtualAddress; tsize = len(tdata)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True; md.skipdata = True

# RaiseException IAT thunk RVAs (to identify the call unambiguously)
raise_rvas = set()
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        nm = (imp.name or b"").decode(errors="replace")
        if nm in ("RaiseException","MessageBoxA","MessageBoxW"):
            raise_rvas.add((nm, imp.address - base))
print("# import thunks:", [(n,hex(r)) for n,r in raise_rvas])

def find_func_start(rva, maxback=0x4000):
    """Walk back to int3 (0xCC) padding boundary -> function start."""
    off = rva - tva
    i = off
    # scan backward for a run of int3 then aligned start
    while i > off - maxback and i > 0:
        if tdata[i-1] == 0xCC and tdata[i-2] == 0xCC:
            return tva + i
        i -= 1
    return tva + (off - maxback if off-maxback>0 else 0)

def disasm_range(start_rva, end_rva):
    off = start_rva - tva
    n = end_rva - start_rva
    out = []
    for ins in md.disasm(tdata[off:off+n], base+start_rva):
        out.append(f"0x{ins.address-base:08x}: {ins.mnemonic:<7} {ins.op_str}")
    return out

targets = {}
# raise-site: the call is just before ret 0x23acaca
targets["raise_site_0x23acaca"] = 0x23acaca
# immediate callers up the chain (read the ones likely the gate body)
for name,rva in [("c01_0x66ba9c",0x66ba9c),("c02_0x66b6e0",0x66b6e0),
                 ("c0f_0x869c39",0x869c39),("c0e_0x532f69",0x532f69)]:
    targets[name]=rva

for name, ret_rva in targets.items():
    fstart = find_func_start(ret_rva)
    print(f"\n===== {name}  ret=0x{ret_rva:08x}  func_start~0x{fstart:08x} =====")
    lines = disasm_range(fstart, ret_rva+0x10)
    # print full for raise_site (small), tail for callers (big)
    if name.startswith("raise"):
        print("\n".join(lines))
    else:
        print("\n".join(lines[-80:]))
