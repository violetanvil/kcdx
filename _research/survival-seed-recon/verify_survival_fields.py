"""verify_survival_fields.py — step 5.2 survival-field RE pass.

Verifies, against WHGame.dll, the per-kind survival datum for each non-function
curated Address Library entity flagged in the step-5.2 brief:

  callsite           ids 5,6,7,8   -> AOB pattern+mask, .text-uniqueness
  string_anchor      id 12         -> literal bytes, .text LEA xref count
  instruction_anchor id 9          -> resolver-chain re-derivation + shape
  data_slot          ids 10,11,132 -> derivation rule (disp-follow / fixed off)
  vtable_base        ids 119,138,139,140 -> contiguous .text-ptr slot count

Image base 0x180000000. RVAs below are stored RVAs (image-base already off).
Reuses the gEnv resolver logic from _research/phase7-recon/find_genv.py and the
pak-resolver gEnv+0x50 fact from _research/phase8.5-pak-resolver/FINDINGS.md.

Static only (pefile + capstone). No live game run.
"""

import struct
import sys
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE_BASE = 0x180000000
DLL = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    r"c:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll")

pe = pefile.PE(str(DLL), fast_load=True)
SEC = {}
for s in pe.sections:
    SEC[s.Name.rstrip(b"\x00").decode()] = s
TEXT = SEC[".text"].get_data()
TEXT_RVA = SEC[".text"].VirtualAddress
RDATA = SEC[".rdata"].get_data()
RDATA_RVA = SEC[".rdata"].VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)


def rva_to_text_off(rva):
    return rva - TEXT_RVA


def read_text(rva, n):
    o = rva_to_text_off(rva)
    return TEXT[o:o + n]


def count_pattern(pat, mask, data=TEXT):
    """Count masked-pattern matches. mask[j]=True => byte must equal pat[j]."""
    n = len(pat)
    hits = []
    for i in range(len(data) - n + 1):
        ok = True
        for j in range(n):
            if mask[j] and data[i + j] != pat[j]:
                ok = False
                break
        if ok:
            hits.append(i)
    return hits


def parse_aob(s):
    """'48 8B 41 08 ? ?' -> (pat bytes, mask list)."""
    pat = bytearray()
    mask = []
    for tok in s.split():
        if tok in ("?", "??"):
            pat.append(0)
            mask.append(False)
        else:
            pat.append(int(tok, 16))
            mask.append(True)
    return bytes(pat), mask


def fmt_aob(pat, mask):
    return " ".join("?" if not mask[i] else f"{pat[i]:02X}" for i in range(len(pat)))


def hexdump(rva, n):
    return " ".join(f"{b:02X}" for b in read_text(rva, n))


print(f"# DLL: {DLL.name}  image_base={hex(IMAGE_BASE)}")
print(f"# .text rva={hex(TEXT_RVA)} size={hex(len(TEXT))}")
print(f"# .rdata rva={hex(RDATA_RVA)} size={hex(len(RDATA))}")
print("=" * 78)

# ---------------------------------------------------------------- callsites
print("\n## CALLSITE ids 5,6,7,8 — AOB + .text-uniqueness\n")

CALLSITES = {
    5: (0x0056174C, "outfit_swap_callsite_aob (16-byte canonical)"),
    6: (0x00561745, "outfit_swap_callsite_context (5's site -7, 23 bytes)"),
    7: (0x005605BC, "IsInCombat_callsite_26b (cmp al,2)"),
    8: (0x00566040, "IsInCombat_callsite_with_stack_frame (cmp al,1, 30 bytes)"),
}
# Prose AOBs to re-verify (from address_names_seed.csv notes):
PROSE_AOB = {
    7: "48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02",
    8: "48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01",
}

for cid, (rva, label) in CALLSITES.items():
    print(f"--- id {cid}: {label}  RVA={hex(rva)}")
    raw32 = hexdump(rva, 32)
    print(f"    bytes@RVA: {raw32}")
    # disassemble a few instructions for shape
    code = read_text(rva, 32)
    insns = list(md.disasm(code, IMAGE_BASE + rva))
    for ins in insns[:8]:
        print(f"      {ins.address:#x}: {ins.mnemonic} {ins.op_str}")
    if cid in PROSE_AOB:
        pat, mask = parse_aob(PROSE_AOB[cid])
        # verify the prose AOB bytes actually match at the stored RVA
        actual = read_text(rva, len(pat))
        match_here = all((not mask[j]) or actual[j] == pat[j] for j in range(len(pat)))
        hits = count_pattern(pat, mask)
        print(f"    prose AOB ({len(pat)}b): {fmt_aob(pat, mask)}")
        print(f"    matches stored RVA: {match_here}   .text hits: {len(hits)} "
              f"{'(UNIQUE)' if len(hits)==1 else '(AMBIGUOUS)' }")
        for h in hits:
            print(f"        hit @ RVA {hex(TEXT_RVA + h)}")
    print()

# For ids 5 & 6 derive the AOB from the bytes at the site, masking nothing first,
# then report the shortest .text-unique prefix.
print("--- id 5/6 derivation: shortest unique prefix from the site bytes")
for cid in (5, 6):
    rva, label = CALLSITES[cid]
    raw = read_text(rva, 48)
    found = None
    for n in range(6, 41):
        pat = raw[:n]
        mask = [True] * n
        hits = count_pattern(pat, mask)
        if len(hits) == 1:
            found = (n, pat, mask)
            break
    if found:
        n, pat, mask = found
        print(f"    id {cid} RVA={hex(rva)}: unique at {n} bytes (no wildcards):")
        print(f"      {fmt_aob(pat, mask)}")
    else:
        print(f"    id {cid} RVA={hex(rva)}: NO unique prefix within 40 bytes")
print()

# ---------------------------------------------------------------- string anchor
print("\n## STRING_ANCHOR id 12 — 'exec autoexec.cfg'\n")
TARGET = b"exec autoexec.cfg"
soff = RDATA.find(TARGET)
str_rva = RDATA_RVA + soff
str_va = IMAGE_BASE + str_rva
# include the trailing NUL to report the literal bytes
lit = RDATA[soff:soff + len(TARGET) + 1]
print(f"    present in .rdata: {soff >= 0}")
print(f"    rva={hex(str_rva)} va={hex(str_va)}")
print(f"    literal bytes (with NUL): {' '.join(f'{b:02X}' for b in lit)}")
print(f"    ascii: {TARGET.decode()!r}")
# also confirm it occurs only once in .rdata
others = []
i = RDATA.find(TARGET)
while i >= 0:
    others.append(i)
    i = RDATA.find(TARGET, i + 1)
print(f"    occurrences in .rdata: {len(others)}")

# count LEA rdx,[rip+disp32] (48 8D 15) and any LEA (48 8D 05/0D/15/1D/...) xrefs
def count_lea_xrefs(target_rva):
    """Count any `REX.W lea reg, [rip+disp32]` whose target == target_rva."""
    hits = []
    i = 0
    # lea r64, [rip+disp32]: 48 8D /r with modrm mod=00 rm=101
    while i <= len(TEXT) - 7:
        if TEXT[i] == 0x48 and TEXT[i + 1] == 0x8D:
            modrm = TEXT[i + 2]
            # mod=00 (top 2 bits 00), rm=101 (low 3 bits 101) -> RIP-relative
            if (modrm & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", TEXT, i + 3)[0]
                ref = TEXT_RVA + i + 7 + disp
                if ref == target_rva:
                    reg = ((modrm >> 3) & 7) | (0)  # no REX.R handling (48 only)
                    hits.append((TEXT_RVA + i, reg))
                i += 7
                continue
        i += 1
    return hits

leas = count_lea_xrefs(str_rva)
print(f"    .text LEA(rip) xrefs to the string: {len(leas)} "
      f"{'(UNIQUE)' if len(leas)==1 else ''}")
for r, reg in leas:
    regn = ["ax","cx","dx","bx","sp","bp","si","di"][reg]
    print(f"        lea r{regn}, [rip->str] @ RVA {hex(r)}")

# ---------------------------------------------------------------- instr anchor
print("\n## INSTRUCTION_ANCHOR id 9 — gEnv_pConsole_mov, resolver chain\n")
# Re-run the 3-step resolver from find_genv.py.
V14_CONTEXT = bytes.fromhex("4C8B921801 0000".replace(" ", ""))
lea_prefix = bytes.fromhex("488D15")
print("    step1: string anchor (id 12) at rva", hex(str_rva))
# step2: LEA rdx,[rip] referencing it
lea_hits = []
i = 0
while i <= len(TEXT) - 7:
    if TEXT[i:i + 3] == lea_prefix:
        disp = struct.unpack_from("<i", TEXT, i + 3)[0]
        if TEXT_RVA + i + 7 + disp == str_rva:
            lea_hits.append(i)
    i += 1
print(f"    step2: `48 8D 15` LEA->string xrefs: {len(lea_hits)}")
for off in lea_hits:
    lea_rva = TEXT_RVA + off
    ctx = TEXT[off - 7:off]
    print(f"      LEA @ RVA {hex(lea_rva)}  preceding7={ctx.hex()}")
    if ctx == V14_CONTEXT:
        mov_off = off - 0x17  # V1.4+ layout: MOV is LEA-0x17
        mov_rva = TEXT_RVA + mov_off
        mov_bytes = TEXT[mov_off:mov_off + 7]
        print(f"      kind=V1.4+ -> pConsole MOV @ RVA {hex(mov_rva)} "
              f"(matches seed id9 RVA 0x86AD99: {mov_rva == 0x86AD99})")
        print(f"      MOV bytes: {' '.join(f'{b:02X}' for b in mov_bytes)}")
        for ins in md.disasm(bytes(mov_bytes), IMAGE_BASE + mov_rva):
            print(f"        decoded: {ins.mnemonic} {ins.op_str}")
        # the expected instruction shape = mov rcx, [rip+disp32] = 48 8B 0D ? ? ? ?
        shape_pat, shape_mask = parse_aob("48 8B 0D ? ? ? ?")
        ok_shape = all((not shape_mask[j]) or mov_bytes[j] == shape_pat[j]
                       for j in range(7))
        print(f"      expected shape '48 8B 0D ?? ?? ?? ??' (mov rcx,[rip]): {ok_shape}")
        # compute pConsole ptr + gEnv from disp
        disp = struct.unpack_from("<i", TEXT, mov_off + 3)[0]
        pconsole_ptr_rva = TEXT_RVA + mov_off + 7 + disp
        print(f"      -> gEnv_pConsole ptr slot rva={hex(pconsole_ptr_rva)} "
              f"(seed id10 = 0x0492B8A8: {pconsole_ptr_rva == 0x0492B8A8})")
        print(f"      -> gEnv = pConsole - 0xA8 = {hex(pconsole_ptr_rva - 0xA8)} "
              f"(seed id11 = 0x0492B800: {pconsole_ptr_rva - 0xA8 == 0x0492B800})")

# ---------------------------------------------------------------- data slots
print("\n## DATA_SLOT ids 10,11,132 — derivation rules\n")
print("    id 10 gEnv_pConsole: follow disp32 from instruction_anchor id 9 (MOV).")
print("      computed above; seed RVA 0x0492B8A8")
print("    id 11 gEnv: id 10 RVA - 0xA8 = "
      f"{hex(0x0492B8A8 - 0xA8)} (seed 0x0492B800: {0x0492B8A8 - 0xA8 == 0x0492B800})")
print("    id 132 gEnv_pCryPak: id 11 (gEnv) RVA + 0x50 = "
      f"{hex(0x0492B800 + 0x50)} (seed 0x0492B850: {0x0492B800 + 0x50 == 0x0492B850})")

# ---------------------------------------------------------------- vtable bases
print("\n## VTABLE_BASE ids 119,138,139,140 — contiguous .text-ptr slot count\n")
# .text VA range for code-pointer test
text_lo = IMAGE_BASE + TEXT_RVA
text_hi = IMAGE_BASE + TEXT_RVA + len(TEXT)


def slot_count(vtable_rva, label, max_slots=128):
    """Read qwords from the vtable RVA; count contiguous .text-range pointers."""
    print(f"--- {label}  RVA={hex(vtable_rva)} (VA {hex(IMAGE_BASE+vtable_rva)})")
    # vtable is in .rdata
    off = vtable_rva - RDATA_RVA
    if off < 0 or off >= len(RDATA):
        print(f"    NOT in .rdata (off={off}); skipping")
        return None
    n = 0
    slots = []
    for s in range(max_slots):
        p = off + s * 8
        if p + 8 > len(RDATA):
            break
        q = struct.unpack_from("<Q", RDATA, p)[0]
        in_text = text_lo <= q < text_hi
        slots.append((q, in_text))
        if in_text:
            n += 1
        else:
            break
    print(f"    contiguous .text-pointing slots: {n}")
    for idx, (q, t) in enumerate(slots[:min(len(slots), n + 2)]):
        rva = q - IMAGE_BASE if t else None
        print(f"      slot[{idx}] = {hex(q)}"
              + (f"  -> .text RVA {hex(rva)}" if t else "   <- NOT .text (table end)"))
    return n


slot_count(0x046AAF00, "id 138 ImodVtable_primary")
slot_count(0x046AAED8, "id 139 ImodVtable_subobject")
slot_count(0x03AA2E60, "id 140 C_ModManager_vtable")
n119 = slot_count(0x03B8AF70, "id 119 CScriptSystem_vtable (prose says 69 slots)")
print()
print("    NOTE id 138/139 are in a writable data section (relocated import"
      " thunks), not .rdata — handled by the off-range message above if so.")
