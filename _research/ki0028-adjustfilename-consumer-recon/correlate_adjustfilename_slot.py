"""KI-0028 DIVERGENCE-C — provenance-verified consumers of CCryPak slot 1 (AdjustFileName, vtable +0x8)
through gEnv->pCryPak.

REUSED instrument from ../ki0028-metadata-consumer-recon/correlate_pcrypak_slots.py (slots 45/67/70),
re-targeted to slot 1 = vtable offset +0x8. Linear capstone DRIFTS on WHGame — byte-scan the
`mov r64,[rip]` loads of the pCryPak global 0x18492B850 (= gEnv 0x18492B800 + 0x50) then correlate
each to `mov vt,[reg]; call [vt+0x8]` to get provenance-verified consumers (call edge AND
receiver==pCryPak both read, AP19-clean).

A genuine gEnv->pCryPak->AdjustFileName(...) site:
  mov  reg, [rip+disp]          ; reg <- *0x18492B850 (pCryPak global)
  ... (short window, reg not clobbered) ...
  mov  vt,  [reg]               ; vt <- vtable
  call qword ptr [vt + 0x8]     ; slot 1 AdjustFileName
OR a direct call [reg+0x8] (some callers keep this in one reg).

Output: every (call VA, enclosing func) pair, deduped by enclosing func.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
PCRYPAK = 0x18492B850
SLOTOFF = 0x8   # slot 1 AdjustFileName

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress
ts = base + tva
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

# 1) byte-scan pcrypak loads: REX.W 8B /r with modrm = 00 reg 101 (rip-rel)
loads = []
for i in range(len(data) - 7):
    if 0x48 <= data[i] <= 0x4f and data[i+1] == 0x8b:
        m = data[i+2]
        if (m & 0xC7) == 0x05:
            disp = int.from_bytes(data[i+3:i+7], "little", signed=True)
            va = ts + i
            if va + 7 + disp == PCRYPAK:
                loads.append(va)

def enclosing_func_start(va):
    off = va - ts
    j = off
    limit = max(0, off - 0x4000)
    while j > limit:
        if data[j] == 0xCC and j+1 <= off and data[j+1] != 0xCC:
            return ts + j + 1
        j -= 1
    return None

WINDOW_BYTES = 96
results = []   # (callva, funcstart, how)
for lva in loads:
    off = lva - ts
    window = data[off: off + WINDOW_BYTES]
    pcreg = None
    vtmap = {}   # vtreg -> base reg it was loaded from
    for ins in md.disasm(window, lva):
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            d, s = ins.operands
            # the pcrypak load itself (seeds pcreg)
            if d.type == X86_OP_REG and s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
                if ins.address + ins.size + s.mem.disp == PCRYPAK:
                    pcreg = d.reg
                    continue
            # vtable load `mov vt,[reg]` disp0
            if d.type == X86_OP_REG and s.type == X86_OP_MEM and s.mem.disp == 0 and s.mem.index == 0 and s.mem.base != 0:
                vtmap[d.reg] = s.mem.base
        if ins.mnemonic == "call" and len(ins.operands) == 1:
            o = ins.operands[0]
            if o.type == X86_OP_MEM and o.mem.base != 0 and o.mem.disp == SLOTOFF:
                breg = o.mem.base
                if pcreg is not None and vtmap.get(breg) == pcreg:
                    fs = enclosing_func_start(ins.address)
                    results.append((ins.address, fs, "vt<-pcrypak"))
                    break
                if breg == pcreg:
                    fs = enclosing_func_start(ins.address)
                    results.append((ins.address, fs, "direct-pcrypak"))
                    break

print(f"pCryPak loads scanned: {len(loads)}")
print(f"slot-1 (AdjustFileName +0x8) provenance-verified call sites: {len(results)}")

# dedup by enclosing func
byfunc = {}
for callva, fs, how in results:
    byfunc.setdefault(fs, []).append((callva, how))
print(f"distinct enclosing funcs: {len(byfunc)}\n")
for fs in sorted(k for k in byfunc if k is not None):
    calls = byfunc[fs]
    print(f"func {fs:#x} (RVA {fs-base:#x}) : {len(calls)} call(s)")
    for callva, how in calls:
        print(f"    call VA {callva:#x}  RVA {callva-base:#x}  {how}")
none = byfunc.get(None)
if none:
    print(f"\n(func ? — int3 boundary not found): {len(none)} call(s)")
    for callva, how in none:
        print(f"    call VA {callva:#x}  RVA {callva-base:#x}  {how}")
