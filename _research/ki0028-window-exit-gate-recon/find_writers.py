"""KI-0028 — find WRITERS of the wedge object 0x549b4a0 and the four entry-guard
singletons (0x492b8c0 / 0x492b8a8 / 0x492b908 / 0x549b4a0), plus the .data flags the
loop's exit branches actually test.

Method (byte-scan, no linear drift — the metadata-recon correlate_pcrypak_slots approach):
for each target global G, scan all of .text for `mov [rip+disp], reg` (opcode 89 /r with
modrm mod=00 rm=101) whose computed target == G. That is a WRITE of a register into G.
Also catch `mov [rip+disp], imm32` (C7 /0). For each write site, find the enclosing
function (back-scan to an int3 pad) and pull its first few string LEAs (KI-0026 identify-
by-strings). Report site + enclosing fn + strings.

Targets:
  WEDGE_OBJ   0x18492b4a0  (real VA: rva 0x549b4a0 + image base) -- the call[rax+0x40] object
  GUARD_C0    0x18492b8c0
  GUARD_A8    0x18492b8a8
  GUARD_908   0x18492b908
  GUARD_880   0x18492b880  (used in body for the lock helper arg build)
  GUARD_890   0x18492b890  (the window-manager singleton PROBE M tracked)
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase  # 0x180000000
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True

text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); tva = text.VirtualAddress; ts = base + tva

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None
def read_cstr(rva, maxn=100):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off+maxn]; end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii","ignore"); return s if s.isprintable() else None
    except Exception: return None

def enclosing(va):
    off = va - ts; j = off; lim = max(0, off - 0x6000)
    while j > lim:
        if data[j] == 0xCC and (j+1) <= off and data[j+1] != 0xCC:
            return ts + j + 1
        j -= 1
    return None

def fn_strings(fn_va, span=0x600, limit=6):
    out = []
    try: d = pe.get_data(fn_va - base, span)
    except Exception: return out
    for ins in md.disasm(d, fn_va):
        if ins.mnemonic == "lea" and "rip" in ins.op_str:
            op = ins.op_str
            try:
                if "rip + " in op: disp = int(op.split("rip + ")[1].split("]")[0],16)
                elif "rip - " in op: disp = -int(op.split("rip - ")[1].split("]")[0],16)
                else: continue
            except Exception: continue
            t = ins.address + ins.size + disp - base
            s = read_cstr(t)
            if s and s not in out:
                out.append(s)
                if len(out) >= limit: break
        if ins.mnemonic == "int3": break
    return out

TARGETS = {
    0x18492b4a0: "WEDGE_OBJ 0x549b4a0",
    0x18492b8c0: "GUARD 0x492b8c0",
    0x18492b8a8: "GUARD 0x492b8a8",
    0x18492b908: "GUARD 0x492b908",
    0x18492b880: "0x492b880",
    0x18492b890: "0x492b890 (winmgr singleton)",
}

# scan for mov [rip+disp32], reg  (REX? 89 modrm(mod=00,reg=r,rm=101) disp32)
# and mov [rip+disp32], imm32     (REX? C7 modrm(mod=00,/0,rm=101) disp32 imm32)
hits = {va: [] for va in TARGETS}
i = 0
n = len(data)
while i < n - 7:
    b = data[i]
    rex = 0
    k = i
    if 0x40 <= b <= 0x4f:
        rex = b; k = i + 1; b = data[k]
    if b == 0x89:  # mov r/m, reg
        modrm = data[k+1]
        if (modrm & 0xC7) == 0x05:  # mod=00, rm=101 => [rip+disp32]
            disp = int.from_bytes(data[k+2:k+6], "little", signed=True)
            site = ts + i
            ilen = (k+6) - i
            tgt = site + ilen + disp
            if tgt in hits:
                reg = ((rex & 4) << 1) | ((modrm >> 3) & 7)
                hits[tgt].append((site, "mov[G],reg", ilen))
            i = k + 6; continue
    if b == 0xC7:  # mov r/m, imm32
        modrm = data[k+1]
        if (modrm & 0xC7) == 0x05 and (modrm & 0x38) == 0x00:
            disp = int.from_bytes(data[k+2:k+6], "little", signed=True)
            imm = int.from_bytes(data[k+6:k+10], "little")
            site = ts + i
            ilen = (k+10) - i
            tgt = site + ilen + disp
            if tgt in hits:
                hits[tgt].append((site, f"mov[G],imm 0x{imm:x}", ilen))
            i = k + 10; continue
    i += 1

for va, label in TARGETS.items():
    lst = hits[va]
    print(f"\n=== WRITERS of {label}  (VA {va:#x}, RVA {va-base:#x}) : {len(lst)} ===")
    for site, kind, ilen in lst:
        fn = enclosing(site)
        ss = fn_strings(fn) if fn else []
        fns = f"fn {fn:#x} (RVA {fn-base:#x})" if fn else "fn ?"
        print(f"  write @ {site:#x} (RVA {site-base:#x})  [{kind}]  in {fns}")
        if ss: print(f"      strings: " + " | ".join(f'"{x}"' for x in ss))
