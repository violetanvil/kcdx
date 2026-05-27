"""Phase 8.5a step 2: from the pak string anchors, find the code that uses them.

Two jobs:
(A) Find LEA xrefs (rip-relative) into the key string VAs:
      ERROR FOpen '%s'  @ 0x1846ABA99  -> the FOpen impl
      System\CryPak.cpp @ 0x183A3B95D  -> CryPak.cpp file-line asserts
      [Mod] Opening paks @ 0x183DBE3D8
    An LEA targeting one of these lands us inside the owning function.
(B) Walk the MSVC RTTI for CCryPak:
      type descriptor `.?AVCCryPak@@`  -> the type-descriptor STRUCT begins
      16 bytes before the mangled name (vfptr, spare, name). Then scan .rdata
      for a Complete Object Locator whose pTypeDescriptor (RVA) points at it,
      and from the COL find the vtable (the qword right after the COL pointer
      in the meta array). Report the vtable VA.

Usage: python find_pak_xrefs.py <WHGame.dll>
"""
import sys, struct
import pefile, capstone

DLL = sys.argv[1]
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

secs = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    sva = image_base + sec.VirtualAddress
    data = sec.get_data()
    secs.append((name, sva, data, sec.VirtualAddress))

def sec_for_va(va):
    for name, sva, data, rva in secs:
        if sva <= va < sva + len(data):
            return name, sva, data
    return None

def read_qword(va):
    s = sec_for_va(va)
    if not s: return None
    _, sva, data = s
    off = va - sva
    if off+8 > len(data): return None
    return struct.unpack_from("<Q", data, off)[0]

def read_dword(va):
    s = sec_for_va(va)
    if not s: return None
    _, sva, data = s
    off = va - sva
    if off+4 > len(data): return None
    return struct.unpack_from("<I", data, off)[0]

text = next(t for t in secs if t[0] == ".text")
_, text_va, text_data, _ = text
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

# ---- (A) LEA xref scan ----
# Targets we want callers/owners of.
LEA_TARGETS = {
    0x1846ABA99: "ERROR FOpen '%s'",
    0x183A3B95D: "System\\CryPak.cpp",
    0x183DBE3D8: "[Mod] Opening paks in",
    0x18404B0A0: "Data.pak' and 'Data/Libs/UI/'",
    0x183A95D10: "CCryPak (rdata str near 0x183A95D10)",
}

print("=== (A) LEA xrefs into pak string anchors ===")
# Linear sweep of .text for `lea reg, [rip+disp32]` (REX.W 8D /r) is too slow at
# 60MB; instead, for each target, search for the 7-byte LEA encodings whose
# computed target == VA. We scan for the disp32 bytes anywhere preceded by a
# plausible LEA opcode. Practical approach: brute scan for `48 8D` then decode.
import re
# Find all occurrences of REX.W LEA (48 8D) and REX.WR LEA (4C 8D) and decode.
lea_hits = {va: [] for va in LEA_TARGETS}
i = 0
n = len(text_data)
# precompute target set
tset = set(LEA_TARGETS)
while True:
    j = text_data.find(b"\x8d", i)
    if j < 0 or j+5 > n:
        break
    # check preceding REX byte for W (48..4f range) at j-1
    if j >= 1 and 0x48 <= text_data[j-1] <= 0x4f:
        modrm = text_data[j+1]
        # rip-relative: mod=00, rm=101
        if (modrm & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", text_data, j+2)[0]
            insn_va = text_va + (j-1)
            insn_len = 7  # rex+8d+modrm+disp32
            tgt = insn_va + insn_len + disp
            if tgt in tset:
                lea_hits[tgt].append(insn_va)
    i = j+1

for va, label in LEA_TARGETS.items():
    print(f"  target 0x{va:X} ({label}): {len(lea_hits[va])} LEA xref(s)")
    for ref in lea_hits[va][:10]:
        print(f"      LEA @ 0x{ref:X}")
print()

# ---- (B) RTTI walk for CCryPak ----
print("=== (B) RTTI walk: CCryPak type descriptor -> COL -> vtable ===")
TD_NAME_VA = 0x184A40150  # ".?AVCCryPak@@"
# type descriptor struct starts 0x10 before the name string (x64: vfptr(8)+spare(8)+name)
TD_VA = TD_NAME_VA - 0x10
print(f"  type descriptor struct @ 0x{TD_VA:X} (name @ 0x{TD_NAME_VA:X})")
td_rva = TD_VA - image_base
print(f"  type descriptor RVA = 0x{td_rva:X}")

# Complete Object Locator (RTTICompleteObjectLocator), x64 layout:
#   +0x00 signature (u32, =1 for x64)
#   +0x04 offset (u32)
#   +0x08 cdOffset (u32)
#   +0x0C pTypeDescriptor (image-relative RVA)  <-- == td_rva
#   +0x10 pClassDescriptor (RVA)
#   +0x14 pSelf (RVA of the COL itself, x64 only)
# Scan .rdata for a dword == td_rva at offset +0x0C of a COL.
rdata = next(t for t in secs if t[0] == ".rdata")
_, rd_va, rd_data, _ = rdata
cols = []
off = 0
needle = struct.pack("<I", td_rva)
while True:
    k = rd_data.find(needle, off)
    if k < 0:
        break
    col_start = k - 0x0C
    if col_start >= 0:
        sig = struct.unpack_from("<I", rd_data, col_start)[0]
        if sig == 1:
            col_va = rd_va + col_start
            cols.append(col_va)
    off = k + 1
print(f"  found {len(cols)} candidate COL(s) pointing at the type descriptor")
for col_va in cols:
    print(f"    COL @ 0x{col_va:X}")
    # The vtable's meta pointer (qword at vtable-8) points at the COL.
    # So scan .rdata (+.data) for a qword == col_va; the vtable starts at +8.
    cqw = struct.pack("<Q", col_va)
    for sname, sva, sdata, _ in secs:
        if sname not in (".rdata", ".data", "_RDATA"):
            continue
        o = 0
        while True:
            m = sdata.find(cqw, o)
            if m < 0:
                break
            meta_va = sva + m
            vtable_va = meta_va + 8
            print(f"      meta ptr @ 0x{meta_va:X}  =>  VTABLE @ 0x{vtable_va:X} [{sname}]")
            # dump first 16 vtable slots
            for slot in range(16):
                fnva = read_qword(vtable_va + slot*8)
                if fnva is None:
                    break
                in_text = (text_va <= fnva < text_va + len(text_data))
                print(f"          slot[{slot:2d}] @ +0x{slot*8:02X} -> 0x{fnva:X}  {'(.text)' if in_text else '(NOT text)'}")
            o = m + 1
print("\ndone.")
