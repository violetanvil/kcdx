"""KI-0028 DIVERGENCE-D — correlate pCryPak-global loads with slot-66 FGetModificationTime calls.

Reused from ki0028-metadata-consumer-recon/correlate_pcrypak_slots.py (the proven, drift-free
instrument). Robust approach (avoids whole-.text linear-disasm drift):
1. Byte-scan for every `mov r64,[rip+d]` whose target == pCryPak global 0x18492B850.
2. Disassemble a LOCAL window seeded AT the load (correct alignment, no drift).
3. Track the loaded reg; find `mov vt,[reg]` (vtable load) then `call [vt + 0x210]`
   (slot 66), OR a direct `call [reg+0x210]`. Report site + enclosing func.

Slot 66 = vtable offset 66*8 = 0x210 = FGetModificationTime.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_REG_RIP

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
PCRYPAK = 0x18492B850
SLOTS = {0x210: "slot66_FGetModificationTime"}

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress
ts = base + tva
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

# 1) byte-scan pcrypak loads
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
results = []
for lva in loads:
    off = lva - ts
    window = data[off: off + WINDOW_BYTES]
    pcreg = None
    vtmap = {}  # vtreg -> base reg it came from
    for ins in md.disasm(window, lva):
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            d, s = ins.operands
            if d.type == X86_OP_REG and s.type == X86_OP_MEM and s.mem.base == X86_REG_RIP:
                if ins.address + ins.size + s.mem.disp == PCRYPAK:
                    pcreg = d.reg
                    continue
            # vtable load `mov vt,[reg]` disp0
            if d.type == X86_OP_REG and s.type == X86_OP_MEM and s.mem.disp == 0 and s.mem.index == 0 and s.mem.base != 0:
                vtmap[d.reg] = s.mem.base
        if ins.mnemonic == "call" and len(ins.operands) == 1:
            o = ins.operands[0]
            if o.type == X86_OP_MEM and o.mem.base != 0 and o.mem.disp in SLOTS:
                breg = o.mem.base
                if pcreg is not None and vtmap.get(breg) == pcreg:
                    fs = enclosing_func_start(ins.address)
                    results.append((SLOTS[o.mem.disp], ins.address, fs, "vt<-pcrypak"))
                    break
                if breg == pcreg:
                    fs = enclosing_func_start(ins.address)
                    results.append((SLOTS[o.mem.disp], ins.address, fs, "direct-pcrypak"))
                    break

print(f"pCryPak loads scanned: {len(loads)}")
print(f"provenance-verified slot-66 consumers: {len(results)}\n")
byslot = {}
for name, callva, fs, how in results:
    byslot.setdefault(name, []).append((callva, fs, how))
for name in ("slot66_FGetModificationTime",):
    lst = byslot.get(name, [])
    print(f"--- {name}: {len(lst)}")
    for callva, fs, how in lst:
        fss = f"func {fs:#x} (RVA {fs-base:#x})" if fs else "func ?"
        print(f"    call VA {callva:#x} RVA {callva-base:#x}  {how}  in {fss}")
    print()
