"""PROBE B follow-up — static walk of the singleton chain.

PROBE B (live) decisively showed:
  - The engine called frame-4 with rcx = 0x7FF8F28B2E60 = the C_ModManager
    VTABLE VA itself (RVA 0x03AA2E60).
  - kcdx's heap obj at 0x1A6FCFCC000 is NEVER read by this path.

The dispatch chain at frame-4:
    mov rcx, [global @ RVA 0x0492B8A8]
    mov rax, [rcx]                       ; vtable
    call [rax + 0xB8]                    ; virtual slot 23
    mov rcx, rax                         ; this = return value (= 0x7FF8F28B2E60 = vtable)

Open questions this static walk answers:
  1. What is the global at RVA 0x0492B8A8? (which section, what's the initial
     value, who writes to it at runtime?)
  2. What does virtual slot 0xB8 of THAT object's vtable do? Where does it
     return the value 0x7FF8F28B2E60 (= C_ModManager vtable) from?
  3. Is there an init path / setter that's supposed to set this global to
     kcdx's modMgr ptr but didn't run, leaving a compile-time `.rdata` default
     pointing at the vtable as a "null sentinel"?
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

GLOBAL_RVA           = 0x0492B8A8   # the singleton pointer the dispatch loads
DISPATCH_FN_RVA      = 0x019C6268   # frame-4 function start
C_MODMGR_VTABLE_RVA  = 0x03AA2E60   # the value returned by virtual+0xB8

pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase


def section_of(rva: int) -> str:
    for s in pe.sections:
        lo = s.VirtualAddress
        hi = lo + s.Misc_VirtualSize
        if lo <= rva < hi:
            return s.Name.rstrip(b"\x00").decode("ascii", errors="replace")
    return "<not in any section>"


def read_at(rva: int, n: int) -> bytes:
    for s in pe.sections:
        lo = s.VirtualAddress
        hi = lo + s.Misc_VirtualSize
        if lo <= rva < hi:
            off = rva - lo
            return s.get_data()[off : off + n]
    return b""


print(f"GLOBAL_RVA          {GLOBAL_RVA:#010x}  section={section_of(GLOBAL_RVA)!r}")
print(f"C_MODMGR_VTABLE_RVA {C_MODMGR_VTABLE_RVA:#010x}  section={section_of(C_MODMGR_VTABLE_RVA)!r}")
print()

# ---- Q1: initial value at GLOBAL_RVA ----------------------------------------
print("=" * 78)
print(f"Initial bytes at GLOBAL_RVA ({GLOBAL_RVA:#x}):")
print("=" * 78)
b = read_at(GLOBAL_RVA, 0x40)
print(f"  hex: {b.hex()}")
if len(b) >= 8:
    iv = int.from_bytes(b[:8], "little")
    print(f"  initial qword: {iv:#018x}")
    if iv == 0:
        print(f"  -> ZERO (bss). Runtime init writes a heap ptr here.")
    elif iv < image_base:
        print(f"  -> below image base, suspicious.")
    elif image_base <= iv < image_base + 0x10000000:
        target_rva = iv - image_base
        print(f"  -> looks like a VA inside this image: RVA {target_rva:#010x}")
        print(f"     section: {section_of(target_rva)!r}")
        if target_rva == C_MODMGR_VTABLE_RVA:
            print(f"     **MATCH** — the global is statically initialized to the")
            print(f"     C_ModManager vtable address itself.")
    else:
        print(f"  -> some other address; check the relocation table.")

# ---- Q2: find writers of GLOBAL_RVA in .text -------------------------------
print()
print("=" * 78)
print(f"Scan .text for writers (mov [GLOBAL_RVA], reg) and readers (mov reg, [GLOBAL_RVA]):")
print("=" * 78)

text_section = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text_section.get_data()
text_va = text_section.VirtualAddress

write_patterns = [
    (b"\x48\x89\x05", "mov [rip+rel32], rax (WRITE)"),
    (b"\x48\x89\x0d", "mov [rip+rel32], rcx (WRITE)"),
    (b"\x48\x89\x15", "mov [rip+rel32], rdx (WRITE)"),
    (b"\x48\x89\x1d", "mov [rip+rel32], rbx (WRITE)"),
    (b"\x48\x89\x35", "mov [rip+rel32], rsi (WRITE)"),
    (b"\x48\x89\x3d", "mov [rip+rel32], rdi (WRITE)"),
    (b"\x4c\x89\x05", "mov [rip+rel32], r8  (WRITE)"),
    (b"\x4c\x89\x0d", "mov [rip+rel32], r9  (WRITE)"),
    (b"\x4c\x89\x1d", "mov [rip+rel32], r11 (WRITE)"),
    (b"\x4c\x89\x25", "mov [rip+rel32], r12 (WRITE)"),
    (b"\x4c\x89\x2d", "mov [rip+rel32], r13 (WRITE)"),
    (b"\x4c\x89\x35", "mov [rip+rel32], r14 (WRITE)"),
    (b"\x4c\x89\x3d", "mov [rip+rel32], r15 (WRITE)"),
    (b"\x48\x8d\x05", "lea rax, [rip+rel32] (ADDR)"),
    (b"\x48\x8d\x0d", "lea rcx, [rip+rel32] (ADDR)"),
    (b"\x48\x8d\x15", "lea rdx, [rip+rel32] (ADDR)"),
    (b"\x48\x8b\x05", "mov rax, [rip+rel32] (READ)"),
    (b"\x48\x8b\x0d", "mov rcx, [rip+rel32] (READ)"),
    (b"\x48\x8b\x15", "mov rdx, [rip+rel32] (READ)"),
    (b"\x48\x8b\x1d", "mov rbx, [rip+rel32] (READ)"),
    (b"\x48\x8b\x35", "mov rsi, [rip+rel32] (READ)"),
    (b"\x48\x8b\x3d", "mov rdi, [rip+rel32] (READ)"),
    (b"\x4c\x8b\x05", "mov r8,  [rip+rel32] (READ)"),
    (b"\x4c\x8b\x0d", "mov r9,  [rip+rel32] (READ)"),
    (b"\x4c\x8b\x35", "mov r14, [rip+rel32] (READ)"),
]

n_writes = 0
n_reads = 0
n_addrs = 0
findings = []
for prefix, desc in write_patterns:
    off = 0
    while True:
        idx = text_data.find(prefix, off)
        if idx == -1:
            break
        if idx + 7 > len(text_data):
            break
        rel32 = int.from_bytes(text_data[idx + 3 : idx + 7], "little", signed=True)
        ins_rva = text_va + idx
        target_rva = ins_rva + 7 + rel32
        if target_rva == GLOBAL_RVA:
            kind = "WRITE" if "(WRITE)" in desc else ("ADDR" if "(ADDR)" in desc else "READ")
            findings.append((kind, ins_rva, desc))
            if kind == "WRITE": n_writes += 1
            elif kind == "ADDR": n_addrs += 1
            else: n_reads += 1
        off = idx + 1

print(f"  total: {len(findings)} references found ({n_writes} writes, {n_reads} reads, {n_addrs} address-of)")
print()
for kind, ins_rva, desc in findings[:60]:
    print(f"    {kind:6s} at RVA {ins_rva:#010x}   {desc}")

if n_writes == 0:
    print()
    print("  ** ZERO WRITES — this global is either compile-time initialized only,")
    print("     OR mutated via a non-mov pattern (XMM move? a different reg I missed?")
    print("     a thunk that doesn't show as a direct mov)? Worth a follow-up.")

# ---- Q3: disasm the FUN at GLOBAL_RVA's vtable+0xB8 -----------------------
# The dispatch is `mov rcx, [GLOBAL_RVA]; mov rax, [rcx]; call [rax+0xB8]`.
# Without a live dump we can't read the vtable directly, but if the global is
# initialized to point at a .rdata vtable, we can find that vtable through the
# write/init patterns.

# If there are NO direct .text writers, this might be a relocation entry. Scan
# the relocation directory for any reloc whose target is in the page containing
# GLOBAL_RVA.

print()
print("=" * 78)
print("Relocation entries touching GLOBAL_RVA's page:")
print("=" * 78)
GLOBAL_PAGE = GLOBAL_RVA & ~0xFFF
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
if hasattr(pe, "DIRECTORY_ENTRY_BASERELOC"):
    matches = 0
    for reloc in pe.DIRECTORY_ENTRY_BASERELOC:
        if reloc.struct.VirtualAddress == GLOBAL_PAGE:
            for entry in reloc.entries:
                print(f"  reloc type {entry.type}  at RVA {entry.rva:#010x}")
                matches += 1
    if matches == 0:
        print(f"  no relocations on page {GLOBAL_PAGE:#x} (this is bss-style — initial value 0)")
else:
    print("  no relocation directory parsed")

# ---- Q4: what does the vtable at virtual+0xB8 do? -------------------------
# Only meaningful once we know which vtable. The candidates:
#   - If the global initial value is 0: it's bss. The runtime init writes a heap
#     ptr; we'd need to find the writer.
#   - If the initial value is a VA: it's a static singleton; the vtable at +0xB8
#     for THAT singleton class is what frame-4 is calling.

# In either case, the value `0x7FF8F28B2E60` (C_ModManager_vtable_VA) is the
# RESULT of calling virtual+0xB8. So somewhere in WHGame.dll there's a function
# whose return value is `lea rax, [rip + <C_ModManager_vtable_offset>]; ret`
# style.

# Find functions whose ONLY content is essentially `lea rax, [rip+rel32]; ret`
# targeting C_MODMGR_VTABLE_RVA. These are the "default getter" candidates.
print()
print("=" * 78)
print(f"Scan .text for `lea rax, [rip+rel32]; ret` returning C_ModManager_vtable")
print("=" * 78)
# pattern: 48 8d 05 ?? ?? ?? ?? c3
hit_count = 0
off = 0
while True:
    idx = text_data.find(b"\x48\x8d\x05", off)
    if idx == -1:
        break
    if idx + 8 > len(text_data):
        break
    # check the byte after the rel32 is `c3` (ret)
    rel32 = int.from_bytes(text_data[idx + 3 : idx + 7], "little", signed=True)
    ins_rva = text_va + idx
    target_rva = ins_rva + 7 + rel32
    ret_byte = text_data[idx + 7]
    if target_rva == C_MODMGR_VTABLE_RVA and ret_byte == 0xc3:
        print(f"    `lea rax, [C_MOD_VT]; ret`  at RVA {ins_rva:#010x}")
        hit_count += 1
    off = idx + 1
if hit_count == 0:
    print("  none found; the virtual returns the vtable via some other path")
    print("  (an offset arithmetic on `this`? a stored member?)")

# Also scan for any direct LEA to C_ModManager_vtable (anywhere in .text).
print()
print("=" * 78)
print(f"All `lea reg, [C_ModManager_vtable]` references in .text:")
print("=" * 78)
hit_count = 0
for prefix, desc in [
    (b"\x48\x8d\x05", "lea rax"),
    (b"\x48\x8d\x0d", "lea rcx"),
    (b"\x48\x8d\x15", "lea rdx"),
    (b"\x48\x8d\x1d", "lea rbx"),
    (b"\x4c\x8d\x05", "lea r8"),
    (b"\x4c\x8d\x0d", "lea r9"),
]:
    off = 0
    while True:
        idx = text_data.find(prefix, off)
        if idx == -1:
            break
        if idx + 7 > len(text_data):
            break
        rel32 = int.from_bytes(text_data[idx + 3 : idx + 7], "little", signed=True)
        ins_rva = text_va + idx
        target_rva = ins_rva + 7 + rel32
        if target_rva == C_MODMGR_VTABLE_RVA:
            hit_count += 1
            if hit_count <= 20:
                print(f"    {desc:10s}  at RVA {ins_rva:#010x}")
        off = idx + 1
print(f"  total: {hit_count} references (writers of C_ModManager vtable into something)")
