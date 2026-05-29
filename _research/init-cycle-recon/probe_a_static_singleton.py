"""PROBE A (static, theory-independent observation) for the post-step-4
WHGame+0x2440C85 AV.

Question: what is the static struct at WHGame.dll RVA 0x024352A0 that the
engine reads `[rbx+0x30..+0x38)` from and walks as a vector<T*>, where each
deref yields function code bytes and AVs?

Ground-truth facts from the minidump (already in
docs/known-issues/post-step-4 AV at WHGame+0x2440C85 in a static CSystem dispatch.md):
- Crash RIP RVA: 0x2440C85 (inside FUN_2440C6C, +0x19 from prologue).
- Caller frames 2..4: FUN_1DBC230 iterates [rsi,rbx) stride 8, calling
  FUN_2440C6C(*iter, fn). Frame 3 FUN_1DBBE20 reads [this+0]=begin and
  [this+8]=end. Frame 4 (at RVA 0x06C66268) does
    `mov rcx, [global @ RVA 0x2474894]`
    `mov rax, [rcx]`
    `call [rax+0xB8]`               ; virtual at slot 0x17 (0xB8/8)
    `lea rcx, [rbx+0x30]`
    `call FUN_1DBBE20`              ; pass rbx+0x30 = the "container" addr
  rbx in the crash was 0x7ff8f1245270 -> RVA 0x02435270.
  rsi = rbx + 0x30 -> RVA 0x024352A0 (where the deref yielded code bytes).

So we need to identify:

  (1) What's at RVA 0x2474894? (global pointer; cdb dps showed it points at
      `WHGame+0x84d3b0` style content, then `[that+0]=vtable`, then virtual at
      +0xB8 returned the 0x7ff8f1245270 static address.) Is it a global
      singleton -- a CSystem-like manager -- with a vtable?

  (2) What's at RVA 0x02435270 (rbx)? Is it a function body (its first bytes
      collide with a prologue), a static const struct, or a .rdata table?
      What does the linker symbol situation look like (look for nearby string
      refs in disassembly)?

  (3) What is `vtable[0x17]` returning, and what does that match in the seed?

The script disassembles enough to:
  - dump the frame-4 function FUN at RVA 0x06C66268 fully so we can read its
    prose role from any string refs it makes
  - identify the global at RVA 0x2474894 as a pointer + read its dword/qword
    initial value (from .data / .rdata, if present)
  - identify whether RVA 0x02435270 is INSIDE a function body (i.e. dispatched
    as `this` is code, which would be a corruption) or is a real static struct
    laid out at that .text/.rdata boundary
  - identify the function at RVA 0x2440C6C and what it expects of *this (the
    `mov rbp, [rcx+0x60]` at +0x19 is its first read; what does it return?)

Theory-INDEPENDENT outcome map -- writing each branch BEFORE running:

OUTCOME 1: RVA 0x02435270 IS in the .text section AND its bytes ARE a real
function prologue.
  Means: the engine was handed a CODE pointer where it expected a STRUCT
  pointer. The dispatch path `[global @ RVA 0x2474894].vtable[+0xB8]()`
  returned a code address as if it were an object. Either kcdx's vtable
  resolution is off (a wrong vtable slot landed at this virtual), or kcdx's
  C_ModManager+vtable interaction is causing the engine to return a code
  address by accident.
  Next: instrument the global at RVA 0x2474894 + its virtual+0xB8 dispatch
  to see what `this` it's called on and what it returns.

OUTCOME 2: RVA 0x02435270 IS a real .rdata/.data static struct (no function
prologue at that exact address; it's a tabled constant) whose +0x30/+0x38
fields are NOT a vector pair but other ints/floats, OR are uninitialized at
runtime because the engine usually constructs an object that "looks like"
this static and overrides them at runtime.
  Means: the engine dispatched on a STATIC DESCRIPTOR that's supposed to be
  the "primordial" prototype before some runtime construction. kcdx replaced
  the wrong thing; the second site is the singleton the global at RVA
  0x2474894 points at, not the C_ModManager whose ctor kcdx hooked.
  Next: trace what writes the global at RVA 0x2474894 -- find that ctor /
  setup site, add a second kcdx bracket if needed.

OUTCOME 3: RVA 0x02435270 is a real function entry, AND the dispatch chain
shows the engine ALWAYS calls this code path as a method on a singleton --
i.e. the singleton's vtable[+0xB8] returns a `&Singleton::Method` (a member
function pointer) which the engine then "calls back" with the singleton as
this. Then `[rbx+0x30]` is `Singleton::field30`, and that field happens to
be at the boundary where the code section starts.
  Means: this is the engine's normal way of saying "get the object" via a
  virtual getter -- the corruption is that the getter, called in a fresh-boot
  context, returned a code address instead of a heap pointer. Likely a
  constant-static-default vs. runtime-initialized mismatch.
  Next: find the path that writes the global at RVA 0x2474894 to a heap
  singleton (normally), and check whether kcdx's reordered init runs that
  init too late.

Print the raw evidence; the outcome that matches the evidence dictates the
fix path, never the reverse.
"""

from __future__ import annotations

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

# Ground-truth RVAs from the .ecxr-derived analysis.
CRASH_FN_START_RVA = 0x02440C6C   # function containing the crash
CRASH_RIP_RVA      = 0x02440C85   # crashing instruction (+0x19 from start)
ITER_FN_START_RVA  = 0x01DBC230   # frame 2: the std::for_each loop
CONTAINER_FN_RVA   = 0x01DBBE20   # frame 3: reads [this+0]=begin, [this+8]=end
DISPATCH_FN_RVA    = 0x019C6268   # frame 4: loads global, virtual call, builds rcx=rbx+0x30
DISPATCH_FN_RETIRE = 0x019C62F0   # +0x88: return point inside CreateInstance+0xC1FD0C
GLOBAL_PTR_RVA     = 0x02474894   # the global the dispatch loads
STATIC_THIS_RVA    = 0x02435270   # rbx in the crash -- the "object" being walked
STATIC_PLUS_30_RVA = 0x024352A0   # rbx+0x30 -- the "vector begin" the engine read

# ---- PE structure load ------------------------------------------------------

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


print("=" * 78)
print("PROBE A: static observation of the WHGame+0x2440C85 dispatch chain")
print("=" * 78)

# ---- Q1: identify each target RVA's section --------------------------------

for label, rva in [
    ("CRASH_RIP",           CRASH_RIP_RVA),
    ("CRASH_FN_START",      CRASH_FN_START_RVA),
    ("ITER_FN_START",       ITER_FN_START_RVA),
    ("CONTAINER_FN_RVA",    CONTAINER_FN_RVA),
    ("DISPATCH_FN_RVA",     DISPATCH_FN_RVA),
    ("DISPATCH_FN_RETIRE",  DISPATCH_FN_RETIRE),
    ("GLOBAL_PTR_RVA",      GLOBAL_PTR_RVA),
    ("STATIC_THIS_RVA",     STATIC_THIS_RVA),
    ("STATIC_PLUS_30_RVA",  STATIC_PLUS_30_RVA),
]:
    print(f"  {label:22s} RVA {rva:#010x}  section={section_of(rva)!r}")

# ---- Q2: what bytes live at STATIC_THIS_RVA (rbx) and STATIC_PLUS_30_RVA? ---

print()
print("-" * 78)
print("Bytes at STATIC_THIS_RVA (rbx in the crash):")
print("-" * 78)
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True

# Disassemble 0x40 bytes from rbx -- if this is a function, we'll see a
# prologue.
bts = read_at(STATIC_THIS_RVA, 0x40)
print(f"  raw bytes (hex): {bts.hex()}")
print(f"  raw bytes (text decode, escaped): {bts!r}")
print()
print(f"  Disassembly (treating as code, 0x40 bytes):")
for ins in md.disasm(bts, image_base + STATIC_THIS_RVA):
    print(f"    {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}")
print()
print("-" * 78)
print("Bytes at STATIC_PLUS_30_RVA (rbx+0x30, where the iteration reads begin):")
print("-" * 78)
bts30 = read_at(STATIC_PLUS_30_RVA, 0x40)
print(f"  raw bytes (hex): {bts30.hex()}")
print(f"  Disassembly (treating as code):")
for ins in md.disasm(bts30, image_base + STATIC_PLUS_30_RVA):
    print(f"    {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}")

# ---- Q3: what is the global at GLOBAL_PTR_RVA? -----------------------------

print()
print("-" * 78)
print("Bytes at GLOBAL_PTR_RVA (the singleton pointer the dispatch loads):")
print("-" * 78)
gptr = read_at(GLOBAL_PTR_RVA, 0x40)
print(f"  raw bytes (hex): {gptr.hex()}")
# Initial qword: at compile time this is usually 0 (zero-init bss); the runtime
# init populates it. Show the qword anyway.
if len(gptr) >= 8:
    initial_qword = int.from_bytes(gptr[:8], "little")
    print(f"  initial qword stored at compile time: {initial_qword:#018x}")
    if initial_qword == 0:
        print(f"  -> ZERO: this is bss, populated at runtime by some init code.")
    else:
        # Could be a relocation. Check if it's a relocation target.
        print(f"  -> NON-ZERO: a static initializer (rare for singletons, but check).")

# ---- Q4: disasm the frame-4 dispatch function ------------------------------

print()
print("-" * 78)
print("Frame-4 dispatch function (DISPATCH_FN_RVA = 0x06C66268), 0xA0 bytes:")
print("-" * 78)
fbody = read_at(DISPATCH_FN_RVA, 0xA0)
for ins in md.disasm(fbody, image_base + DISPATCH_FN_RVA):
    rip_delta = ins.address - image_base - DISPATCH_FN_RETIRE
    marker = "  <-- return point (frame-3 RetAddr)" if ins.address - image_base == DISPATCH_FN_RETIRE else ""
    print(f"    {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}{marker}")
    if ins.address - image_base > DISPATCH_FN_RETIRE + 0x10:
        break

# ---- Q5: disasm the crash function (FUN_2440C6C) ---------------------------

print()
print("-" * 78)
print("Crash function (FUN_2440C6C, full body up to the next ret):")
print("-" * 78)
cbody = read_at(CRASH_FN_START_RVA, 0x200)
seen_rets = 0
for ins in md.disasm(cbody, image_base + CRASH_FN_START_RVA):
    marker = "  <-- CRASH" if ins.address - image_base == CRASH_RIP_RVA else ""
    print(f"    {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<8} {ins.op_str}{marker}")
    if ins.mnemonic == "ret":
        seen_rets += 1
        if seen_rets >= 1:
            break
    if ins.address - image_base - CRASH_FN_START_RVA > 0x180:
        break

# ---- Q6: cross-reference: who else stores to GLOBAL_PTR_RVA? --------------
# A `mov [GLOBAL_PTR_RVA], <reg>` is rare and identifies the init site.
# Scan the .text section for `48 89 05 <rel32>` (mov [rip+disp32], rax) and
# `48 89 0d <rel32>` etc. where target RVA == GLOBAL_PTR_RVA.

print()
print("-" * 78)
print(f"Scan .text for `mov [GLOBAL_PTR_RVA], rax` / `lea rcx, [GLOBAL_PTR_RVA]`")
print("-" * 78)
text_section = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text_section.get_data()
text_va = text_section.VirtualAddress
# Look for `48 89 05 ?? ?? ?? ??`  (mov [rip+rel32], rax) targeting GLOBAL_PTR_RVA.
write_patterns = [
    (b"\x48\x89\x05", "mov [rip+rel32], rax"),
    (b"\x48\x89\x0d", "mov [rip+rel32], rcx"),
    (b"\x48\x89\x15", "mov [rip+rel32], rdx"),
    (b"\x48\x89\x1d", "mov [rip+rel32], rbx"),
    (b"\x48\x89\x25", "mov [rip+rel32], rsp"),
    (b"\x48\x89\x2d", "mov [rip+rel32], rbp"),
    (b"\x48\x89\x35", "mov [rip+rel32], rsi"),
    (b"\x48\x89\x3d", "mov [rip+rel32], rdi"),
    (b"\x4c\x89\x05", "mov [rip+rel32], r8"),
    (b"\x4c\x89\x0d", "mov [rip+rel32], r9"),
    (b"\x48\x8d\x05", "lea rax, [rip+rel32]"),
    (b"\x48\x8d\x0d", "lea rcx, [rip+rel32]"),
    (b"\x48\x8b\x05", "mov rax, [rip+rel32]"),
    (b"\x48\x8b\x0d", "mov rcx, [rip+rel32]"),
    (b"\x48\x8b\x1d", "mov rbx, [rip+rel32]"),
]

ref_count = 0
for prefix, desc in write_patterns:
    off = 0
    while True:
        idx = text_data.find(prefix, off)
        if idx == -1:
            break
        # The rel32 lives at idx+3 .. idx+7; instruction is 7 bytes; target =
        # instruction_addr + 7 + rel32.
        if idx + 7 > len(text_data):
            break
        rel32 = int.from_bytes(text_data[idx + 3 : idx + 7], "little", signed=True)
        ins_rva = text_va + idx
        target_rva = ins_rva + 7 + rel32
        if target_rva == GLOBAL_PTR_RVA:
            ref_count += 1
            print(f"    {desc:30s} at RVA {ins_rva:#010x}  -> {target_rva:#010x}")
            if ref_count > 30:
                print("    (more than 30 references, truncating)")
                break
        off = idx + 1
    if ref_count > 30:
        break

if ref_count == 0:
    print("    NO references to GLOBAL_PTR_RVA found in .text via simple mov/lea patterns.")
    print("    -> Probably accessed via a different addressing form, or via a thunk.")

# ---- Q7: cross-reference: who else stores to / reads from STATIC_THIS_RVA --
# A function calling this dispatch might do `lea rcx, [STATIC_THIS_RVA]` or
# `mov rcx, [STATIC_THIS_RVA]`. Scan for the same patterns.

print()
print("-" * 78)
print(f"Scan .text for references to STATIC_THIS_RVA ({STATIC_THIS_RVA:#x})")
print("-" * 78)
ref_count = 0
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
        if target_rva == STATIC_THIS_RVA:
            ref_count += 1
            print(f"    {desc:30s} at RVA {ins_rva:#010x}  -> {target_rva:#010x}")
            if ref_count > 30:
                print("    (more than 30 references, truncating)")
                break
        off = idx + 1
    if ref_count > 30:
        break

if ref_count == 0:
    print("    NO references to STATIC_THIS_RVA found in .text via simple mov/lea patterns.")
    print("    -> This RVA is NOT used as a static address by any code; the runtime")
    print("       value at rbx came from elsewhere (the singleton's virtual returned it).")

# ---- Q8: what's at GLOBAL_PTR_RVA's section + symbol context --------------

print()
print("-" * 78)
print("Summary")
print("-" * 78)
print(f"  GLOBAL_PTR_RVA section: {section_of(GLOBAL_PTR_RVA)!r}")
print(f"  STATIC_THIS_RVA section: {section_of(STATIC_THIS_RVA)!r}")
print(f"  CRASH_FN_START_RVA section: {section_of(CRASH_FN_START_RVA)!r}")
print(f"  DISPATCH_FN_RVA section: {section_of(DISPATCH_FN_RVA)!r}")
print()
print("Outcome to read off the above:")
print(" - If STATIC_THIS_RVA is in .text AND disasm shows a clean function prologue,")
print("   the runtime singleton at GLOBAL_PTR_RVA somehow returned a CODE ADDRESS as")
print("   this -> the virtual+0xB8 dispatch is broken or kcdx's init order is wrong.")
print(" - If STATIC_THIS_RVA is in .rdata or .data, it's a real static struct that")
print("   the engine reads `[this+0x30..+0x38)` from -> kcdx's bracket replaced one")
print("   site (ModManager_ctor) but missed the static-table init.")
print(" - If `mov [GLOBAL_PTR_RVA], rax` appears in .text, name that writer site:")
print("   that's where the global gets its runtime value. Hook there, not at ctor.")
