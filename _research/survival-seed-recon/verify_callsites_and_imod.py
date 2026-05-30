"""verify_callsites_and_imod.py — tighten two results from the first pass:

(A) ids 5 & 6 callsite AOBs: verify the PROSE-defined spans (16-byte canonical
    for id 5; that span -7 bytes for id 6) with their disp32 wildcards masked,
    and report .text uniqueness — that is the actual survival datum to store,
    not my forward-derived prefix.

(B) ids 138 & 139 (I_Mod vtables): the first pass found only 3 / 4 contiguous
    .text-pointing qwords, contradicting the init-cycle probe ("first 4 read as
    code"). Investigate: dump the qwords AND check the PE base-relocation table
    to see whether each vtable qword is a relocated absolute pointer (so the
    on-disk value is already image-based and my .text-range test is valid) and
    find where the table actually ends (next reloc gap / RTTI boundary).
"""

import struct
import sys
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE_BASE = 0x180000000
DLL = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    r"c:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll")
pe = pefile.PE(str(DLL))  # full load -> includes relocations
SEC = {s.Name.rstrip(b"\x00").decode(): s for s in pe.sections}
TEXT = SEC[".text"].get_data(); TEXT_RVA = SEC[".text"].VirtualAddress
RDATA = SEC[".rdata"].get_data(); RDATA_RVA = SEC[".rdata"].VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
text_lo = IMAGE_BASE + TEXT_RVA
text_hi = IMAGE_BASE + TEXT_RVA + len(TEXT)


def read_text(rva, n):
    return TEXT[rva - TEXT_RVA: rva - TEXT_RVA + n]


def parse_aob(s):
    pat = bytearray(); mask = []
    for tok in s.split():
        if tok in ("?", "??"):
            pat.append(0); mask.append(False)
        else:
            pat.append(int(tok, 16)); mask.append(True)
    return bytes(pat), mask


def fmt_aob(pat, mask):
    return " ".join("?" if not mask[i] else f"{pat[i]:02X}" for i in range(len(pat)))


def count_pattern(pat, mask, data=TEXT):
    n = len(pat); hits = []
    for i in range(len(data) - n + 1):
        if all((not mask[j]) or data[i + j] == pat[j] for j in range(n)):
            hits.append(i)
    return hits


print("## (A) callsite ids 5 & 6 — PROSE spans, disp32 masked\n")

# id 5: "16-byte canonical AOB", offset +13 is 'mov r14b, al'. The site at
# RVA 0x56174c starts: 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0
#   add rcx,0xb60 / mov rax,[rcx] / call [rax+8] / mov r14b,al
# No RIP-relative disp in the first 16 bytes -> no wildcards needed.
RAW5 = read_text(0x0056174C, 16)
print(f"id 5 RVA 0x56174c, first 16 bytes: {' '.join(f'{b:02X}' for b in RAW5)}")
for ins in md.disasm(bytes(RAW5), IMAGE_BASE + 0x56174C):
    print(f"    {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
pat5, mask5 = bytes(RAW5), [True] * 16
hits5 = count_pattern(pat5, mask5)
print(f"  16-byte AOB: {fmt_aob(pat5, mask5)}")
print(f"  .text hits: {len(hits5)} {'(UNIQUE)' if len(hits5)==1 else '(AMBIGUOUS)'} "
      f"at {[hex(TEXT_RVA+h) for h in hits5]}")
print()

# id 6: id 5's site extended 7 bytes upward => start at 0x561745, 23 bytes.
RAW6 = read_text(0x00561745, 23)
print(f"id 6 RVA 0x561745, 23 bytes: {' '.join(f'{b:02X}' for b in RAW6)}")
for ins in md.disasm(bytes(RAW6), IMAGE_BASE + 0x561745):
    print(f"    {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
pat6, mask6 = bytes(RAW6), [True] * 23
hits6 = count_pattern(pat6, mask6)
print(f"  23-byte AOB: {fmt_aob(pat6, mask6)}")
print(f"  .text hits: {len(hits6)} {'(UNIQUE)' if len(hits6)==1 else '(AMBIGUOUS)'} "
      f"at {[hex(TEXT_RVA+h) for h in hits6]}")
# also report 16-byte id5 uniqueness sanity: is 16 already unique? if so id6's
# extension is belt-and-suspenders; if id5 was NOT unique, id6 exists to fix it.
print()

print("## (B) I_Mod vtables 138 (0x46AAF00) / 139 (0x46AAED8) — reloc-aware\n")

# Collect the relocation RVAs (each entry = an absolute64 fixup site).
pe.parse_data_directories(
    directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
reloc_rvas = set()
for block in pe.DIRECTORY_ENTRY_BASERELOC:
    for e in block.entries:
        if e.type == 10:  # IMAGE_REL_BASED_DIR64
            reloc_rvas.add(e.rva)


def dump_vtable(name, vt_rva, n=24):
    print(f"--- {name}  RVA={hex(vt_rva)}")
    # which section?
    sec = next((nm for nm, s in SEC.items()
                if s.VirtualAddress <= vt_rva < s.VirtualAddress + s.Misc_VirtualSize), "?")
    print(f"    section: {sec}")
    data = SEC[sec].get_data(); base = SEC[sec].VirtualAddress
    off = vt_rva - base
    contiguous = 0
    ended = False
    for i in range(n):
        slot_rva = vt_rva + i * 8
        q = struct.unpack_from("<Q", data, off + i * 8)[0]
        is_reloc = slot_rva in reloc_rvas
        in_text = text_lo <= q < text_hi
        tag = ""
        if is_reloc and in_text:
            tag = f"-> .text RVA {hex(q - IMAGE_BASE)}"
        elif is_reloc:
            tag = f"-> reloc, but NOT .text (RVA {hex(q - IMAGE_BASE)})"
        else:
            tag = "(no reloc here — NOT a pointer slot; table boundary)"
        # contiguity: a real vtable slot is a reloc'd .text pointer
        if not ended and is_reloc and in_text:
            contiguous += 1
        elif not ended:
            ended = True
        print(f"    slot[{i:2d}] @ {hex(slot_rva)} = {hex(q):>18}  reloc={int(is_reloc)} {tag}")
        if ended and i > contiguous + 2:
            break
    print(f"    => contiguous reloc'd .text-pointer slots: {contiguous}")
    return contiguous

# show a few qwords BEFORE the vtable too (RTTI COL pointer usually sits at -8)
def dump_preamble(name, vt_rva):
    sec = next((nm for nm, s in SEC.items()
                if s.VirtualAddress <= vt_rva < s.VirtualAddress + s.Misc_VirtualSize), "?")
    data = SEC[sec].get_data(); base = SEC[sec].VirtualAddress
    q = struct.unpack_from("<Q", data, (vt_rva - 8) - base)[0]
    is_reloc = (vt_rva - 8) in reloc_rvas
    print(f"    preamble slot[-1] @ {hex(vt_rva-8)} = {hex(q)} reloc={int(is_reloc)} "
          f"(RTTI COL ptr if reloc'd to .rdata)")

dump_preamble("138", 0x046AAF00)
dump_vtable("138 ImodVtable_primary", 0x046AAF00)
print()
dump_preamble("139", 0x046AAED8)
dump_vtable("139 ImodVtable_subobject", 0x046AAED8)
