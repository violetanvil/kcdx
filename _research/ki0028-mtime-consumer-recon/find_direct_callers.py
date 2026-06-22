"""Scan whole .text for DIRECT references to the engine FGetModificationTime body 0x18241a3bc:
 - rel32 `call`/`jmp` whose target == body  (E8/E9 + disp32)
 - `mov reg,[rip+d]`/`lea reg,[rip+d]` whose target == body (address-taken, e.g. a wrapper helper)
Byte-scan based (drift-free): we don't trust linear disasm; we match the rel32/riprel encodings directly.
This catches mtime reached NOT through the vtable (a wrapped CCryPak helper calling the body directly)."""
import pefile
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
BODY = 0x18241a3bc
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
ts = base + text.VirtualAddress

rel32_calls = []   # E8
rel32_jmps = []    # E9
riprel_refs = []   # mov/lea reg,[rip+d]

n = len(data)
for i in range(n - 5):
    b = data[i]
    # E8 call rel32 / E9 jmp rel32 : target = (va of next insn) + disp32
    if b in (0xE8, 0xE9):
        disp = int.from_bytes(data[i+1:i+5], "little", signed=True)
        nxt = ts + i + 5
        if nxt + disp == BODY:
            (rel32_calls if b == 0xE8 else rel32_jmps).append(ts + i)
# rip-relative mov/lea: 48 8b /r modrm 05 (or 48 8d for lea). scan for 48 8b ?? 05 and 48 8d ?? 05
for i in range(n - 7):
    if data[i] in range(0x48, 0x50) and data[i+1] in (0x8b, 0x8d):
        if (data[i+2] & 0xC7) == 0x05:
            disp = int.from_bytes(data[i+3:i+7], "little", signed=True)
            va = ts + i
            if va + 7 + disp == BODY:
                riprel_refs.append((va, "mov" if data[i+1]==0x8b else "lea"))

print(f"engine FGetModificationTime body = {BODY:#x} (RVA {BODY-base:#x})")
print(f"direct rel32 CALL sites: {len(rel32_calls)}")
for v in rel32_calls: print(f"    call {v:#x} (RVA {v-base:#x})")
print(f"direct rel32 JMP (tail-call) sites: {len(rel32_jmps)}")
for v in rel32_jmps: print(f"    jmp  {v:#x} (RVA {v-base:#x})")
print(f"address-taken (mov/lea rip-rel) sites: {len(riprel_refs)}")
for v,k in riprel_refs: print(f"    {k}  {v:#x} (RVA {v-base:#x})")
