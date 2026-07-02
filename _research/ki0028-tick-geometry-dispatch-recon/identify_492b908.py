"""KI-0028 Measurement 2 — identify singleton 0x492b908 (the tick's per-frame render gate at
0x667ed0: cmp qword [0x492b908], r14; je 0x667f84 -> skips a block of vtable calls when null).

The tick body 0x667b24 gates a block on [0x492b908] being non-null. To know whether this is the
geometry-dispatch gate, identify WHAT 0x492b908 is:
  1. Find its WRITER (mov [0x492b908], rax) — the fn that installs it names the subsystem via
     nearby strings (like 0x549b498 was named by CSystem::Init strings in the wedge recon).
  2. Find the vtable-slot calls the gated block makes on it ([rax+0x430], +0x240, +0x250, +0x248,
     +0x428) — the slot offsets characterize the interface (I3DEngine / IRenderer / ISystem).

Static only — this identifies the object + its role. Whether it is null at RUNTIME swap-on vs
swap-off is the /debug probe that follows (chain link 2).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True); pe.parse_data_directories()
base = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); tva = text.VirtualAddress
TARGET = 0x492b908

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None
def read_cstr(rva, maxn=80):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off+maxn]; end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii", "ignore"); return s if s.isprintable() else None
    except Exception: return None
def rip_rva(insn):
    op = insn.op_str
    if "rip + " in op:
        try: d = int(op.split("rip + ")[1].split("]")[0], 16)
        except Exception: return None
    elif "rip - " in op:
        try: d = -int(op.split("rip - ")[1].split("]")[0], 16)
        except Exception: return None
    else: return None
    return (insn.address + insn.size - base) + d

# Scan .text for a WRITE to [rip+..] == TARGET (mov [mem], reg where mem resolves to TARGET).
print(f"=== writers of 0x{TARGET:x} (mov [rip->0x{TARGET:x}], reg) ===")
writers = []
for insn in md.disasm(tdata, base + tva):
    if insn.mnemonic == "mov" and insn.op_str.startswith("qword ptr [rip"):
        tgt = rip_rva(insn)
        if tgt == TARGET:
            writers.append(insn.address - base)
            print(f"  WRITE @ 0x{insn.address-base:x}: {insn.mnemonic} {insn.op_str}")

# For each writer, find the nearest strings in the ±0x400 window (subsystem name).
for w in writers:
    print(f"\n--- context strings near writer 0x{w:x} (±0x400) ---")
    lo = max(0, w - 0x400)
    d = pe.get_data(lo, (w + 0x400) - lo)
    for insn in md.disasm(d, base + lo):
        tgt = rip_rva(insn)
        if tgt is not None:
            s = read_cstr(tgt)
            if s and len(s) >= 4:
                print(f"    0x{insn.address-base:x} -> \"{s}\"")

if not writers:
    print("  (no direct .text writer — installed via a base+offset table like the guard singletons)")
