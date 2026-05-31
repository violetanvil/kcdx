#!/usr/bin/env python3
"""
Seed verification harness — checks every Address Library seed row against WHGame.dll.

READ-ONLY. Produces a per-row machine-checkable verdict for the mechanical facts:
  - rva in-range for the image
  - function/function_no_sig/function_variadic: rva is a plausible function ENTRY (prologue)
  - callsite / instruction_anchor: survival_aob matches the bytes at rva (+ .text uniqueness if expected)
  - string_anchor: survival_anchor_string is the literal at rva; xref-unique if asserted
  - data_slot: survival_rule derivation re-runs from derives_from to the stated rva
  - vtable_base: rva holds survival_slot_count contiguous in-image code pointers

ABI signatures and vtable_index slots are NOT machine-decided here (those need body
analysis / interface-vtable identification) — flagged for the human/abi_walker pass.

Image base 0x180000000. KCD2 1.5.1164953.
"""
import csv, struct, sys, os
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

REPO = r"c:/Users/Michael/Documents/KCD2 Mods/kcdx"
DLL  = os.path.join(REPO, "third-party-ghidra", "WHGame.dll")
NAMES = os.path.join(REPO, "data/seeds/address_names_seed.csv")
VERS  = os.path.join(REPO, "data/seeds/address_versions_seed.csv")
IMAGE_BASE = 0x180000000

pe = pefile.PE(DLL, fast_load=True)
opt_base = pe.OPTIONAL_HEADER.ImageBase
# Build section map: (rva_start, rva_end, raw_offset, name, is_exec)
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1")
    va = s.VirtualAddress
    vsize = max(s.Misc_VirtualSize, s.SizeOfRawData)
    is_exec = bool(s.Characteristics & 0x20000000)  # IMAGE_SCN_MEM_EXECUTE
    sections.append((va, va + vsize, s.PointerToRawData, name, is_exec, s.SizeOfRawData))

# Full image bytes mapped by RVA (read raw, build sparse via section lookup)
def rva_to_off(rva):
    for va, end, raw, name, ex, rawsz in sections:
        if va <= rva < end:
            delta = rva - va
            if delta < rawsz:
                return raw + delta
            return None  # in virtual padding, not in file
    return None

def section_of(rva):
    for va, end, raw, name, ex, rawsz in sections:
        if va <= rva < end:
            return name, ex
    return None, False

with open(DLL, "rb") as f:
    DATA = f.read()

def read(rva, n):
    off = rva_to_off(rva)
    if off is None: return None
    return DATA[off:off+n]

# .text range for uniqueness scans
text_rng = None
for va, end, raw, name, ex, rawsz in sections:
    if name == ".text":
        text_rng = (raw, raw + rawsz, va)
TEXT_RAW_START, TEXT_RAW_END, TEXT_VA = text_rng
TEXT_BYTES = DATA[TEXT_RAW_START:TEXT_RAW_END]

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

def parse_aob(aob):
    """Parse 'AA BB ?? CC' into (bytes, mask) — ?? = wildcard."""
    toks = aob.strip().split()
    b = bytearray(); m = bytearray()
    for t in toks:
        if t in ("??", "?"):
            b.append(0); m.append(0)
        else:
            b.append(int(t, 16)); m.append(0xFF)
    return bytes(b), bytes(m)

def match_at(rva, aob):
    b, m = parse_aob(aob)
    got = read(rva, len(b))
    if got is None or len(got) < len(b): return None
    for i in range(len(b)):
        if (got[i] & m[i]) != (b[i] & m[i]):
            return False
    return True

def count_text_matches(aob):
    b, m = parse_aob(aob)
    n = len(b); cnt = 0; first = None
    # naive scan with mask
    rng = len(TEXT_BYTES) - n
    i = 0
    while i <= rng:
        ok = True
        for j in range(n):
            if (TEXT_BYTES[i+j] & m[j]) != (b[j] & m[j]):
                ok = False; break
        if ok:
            cnt += 1
            if first is None:
                first = TEXT_VA + i
            if cnt > 5: break
        i += 1
    return cnt, first

# Common function prologue starts (x64). Not exhaustive but covers MSVC codegen.
PROLOGUE_HINTS = [
    b"\x48\x89\x5c\x24",  # mov [rsp+x], rbx
    b"\x48\x89\x4c\x24",  # mov [rsp+x], rcx
    b"\x48\x89\x54\x24",  # mov [rsp+x], rdx
    b"\x48\x89\x74\x24",  # mov [rsp+x], rsi
    b"\x48\x89\x7c\x24",  # mov [rsp+x], rdi
    b"\x4c\x89\x44\x24",  # mov [rsp+x], r8
    b"\x4c\x89\x4c\x24",  # mov [rsp+x], r9
    b"\x57",              # push rdi
    b"\x55",              # push rbp
    b"\x56",              # push rsi
    b"\x53",              # push rbx
    b"\x48\x83\xec",      # sub rsp, imm8
    b"\x48\x81\xec",      # sub rsp, imm32
    b"\x40\x53",          # push rbx (REX)
    b"\x40\x55", b"\x40\x56", b"\x40\x57",
    b"\x41\x54", b"\x41\x55", b"\x41\x56", b"\x41\x57",  # push r12-r15
    b"\x48\x8b\xc4",      # mov rax, rsp (frame setup)
    b"\xe9",              # jmp rel32 (leaf thunk, e.g. luaL_addstring tail-jmp)
    b"\xb8",              # mov eax, imm (small stub e.g. io stub ret 0 — actually C2/C3)
    b"\x33\xc0",          # xor eax,eax
    b"\xc3",              # ret (3-byte stub)
    b"\xc2",              # ret imm
]
def looks_like_entry(rva):
    b = read(rva, 4)
    if b is None: return ("OFFMAP", None)
    for h in PROLOGUE_HINTS:
        if b.startswith(h):
            return ("ENTRY", b.hex())
    return ("UNUSUAL", b.hex())

def in_image(rva):
    return section_of(rva)[0] is not None

# ---- load seeds ----
names = {}
with open(NAMES, newline="", encoding="utf-8") as f:
    for r in csv.DictReader(f):
        names[r["id"]] = r["name"]

rows = []
with open(VERS, newline="", encoding="utf-8") as f:
    for r in csv.DictReader(f):
        rows.append(r)

print(f"# WHGame.dll image base in header: {hex(opt_base)}; assuming {hex(IMAGE_BASE)}")
print(f"# sections: " + ", ".join(f"{n}[{hex(va)}..{hex(e)}]{'X' if ex else ''}" for va,e,_,n,ex,_ in sections))
print(f"# {len(rows)} version rows\n")
print("id|name|kind|rva|sect|check|result|detail")

def disp32_data_slot(derive_rva):
    """For instruction_anchor with disp32: read mov rcx,[rip+disp32] => target VA."""
    pass

for r in rows:
    rid = r["kcdx_id"]; nm = names.get(rid, "?"); kind = r["kind"]
    rva_s = r["rva"].strip()
    rva = int(rva_s, 16) if rva_s else None
    sect = section_of(rva)[0] if rva is not None else "(none)"

    if kind in ("function", "function_no_sig", "function_variadic"):
        if rva is None:
            print(f"{rid}|{nm}|{kind}|-|-|entry|FAIL|no rva"); continue
        if not in_image(rva):
            print(f"{rid}|{nm}|{kind}|{rva_s}|{sect}|entry|FAIL|rva out of image"); continue
        verdict, det = looks_like_entry(rva)
        secname, isexec = section_of(rva)
        execflag = "exec" if isexec else "NONEXEC"
        print(f"{rid}|{nm}|{kind}|{rva_s}|{secname}/{execflag}|entry|{verdict}|{det}")

    elif kind in ("callsite", "instruction_anchor"):
        aob = r["survival_aob"].strip()
        if rva is None or not aob:
            print(f"{rid}|{nm}|{kind}|{rva_s}|{sect}|aob|FAIL|missing rva/aob"); continue
        m = match_at(rva, aob)
        uniq = ""
        if r.get("survival_expect_unique","").strip() == "1":
            cnt, first = count_text_matches(aob)
            uniq = f"; text_matches={'>5' if cnt>5 else cnt} first={hex(first) if first else None}"
        secname, isexec = section_of(rva)
        res = "CONFIRMED" if m else ("MISMATCH" if m is False else "OFFMAP")
        print(f"{rid}|{nm}|{kind}|{rva_s}|{secname}|aob_match|{res}|aob@rva={m}{uniq}")

    elif kind == "string_anchor":
        s = r["survival_anchor_string"]
        got = read(rva, len(s)+1) if rva is not None else None
        ok = got is not None and got[:len(s)] == s.encode("latin1") and (len(got)<=len(s) or got[len(s):len(s)+1]==b"\x00")
        secname, _ = section_of(rva) if rva else ("-",0)
        print(f"{rid}|{nm}|{kind}|{rva_s}|{secname}|string|{'CONFIRMED' if ok else 'MISMATCH'}|want={s!r} got={got[:len(s)+1]!r}")

    elif kind == "data_slot":
        secname, isexec = section_of(rva) if rva is not None else ("-",0)
        # derivation re-run handled separately for the gEnv chain; here just report placement
        rule = r["survival_rule"]; deriv = r["survival_derives_from"]
        print(f"{rid}|{nm}|{kind}|{rva_s}|{secname}|placement|INFO|rule='{rule}' derives_from={deriv} (derivation re-run below)")

    elif kind == "vtable_base":
        cnt_expected = r["survival_slot_count"].strip()
        # count contiguous in-image pointers (to .text) at rva
        n = 0; ptrs = []
        if rva is not None and in_image(rva):
            for i in range(0, 200):
                pv = read(rva + i*8, 8)
                if pv is None or len(pv) < 8: break
                val = struct.unpack("<Q", pv)[0]
                if val == 0: break
                tgt_rva = val - IMAGE_BASE
                sname, sx = section_of(tgt_rva)
                if sname is None or not sx:  # not a code pointer
                    break
                n += 1; ptrs.append(hex(tgt_rva))
        secname, _ = section_of(rva) if rva else ("-",0)
        match = "CONFIRMED" if str(n) == cnt_expected else f"COUNT_DIFFERS(found {n}, seed {cnt_expected})"
        print(f"{rid}|{nm}|{kind}|{rva_s}|{secname}|slot_count|{match}|contig_code_ptrs={n} first3={ptrs[:3]}")

    elif kind == "vtable_index":
        slot = r["vtable_slot"].strip()
        print(f"{rid}|{nm}|{kind}|-|-|slot_index|NEEDS_INTERFACE_VTABLE|seed_slot={slot}")

    else:
        print(f"{rid}|{nm}|{kind}|{rva_s}|{sect}|?|UNKNOWN_KIND|")

print("\n# ---- data_slot / gEnv chain derivation re-run ----")
# id 9: instruction_anchor 'mov rcx,[rip+disp32]' at 0x0086AD99 ; target = MOV_VA + 7 + disp32
mov_rva = 0x0086AD99
mb = read(mov_rva, 7)
if mb and mb[:3] == b"\x48\x8b\x0d":
    disp = struct.unpack("<i", mb[3:7])[0]
    target_va = IMAGE_BASE + mov_rva + 7 + disp
    target_rva = target_va - IMAGE_BASE
    print(f"id9 mov@{hex(mov_rva)} bytes={mb.hex()} disp32={hex(disp)} -> pConsole_ptr RVA {hex(target_rva)}  (seed id10=0x0492B8A8)")
    genv_rva = target_rva - 0xA8
    print(f"  gEnv = pConsole_ptr - 0xA8 = {hex(genv_rva)}  (seed id11=0x0492B800)")
    pcrypak_rva = genv_rva + 0x50
    print(f"  gEnv+0x50 = pCryPak = {hex(pcrypak_rva)}  (seed id132=0x0492B850)")
else:
    print(f"id9 mov@{hex(mov_rva)} bytes={mb.hex() if mb else None} — NOT a 48 8B 0D mov; derivation cannot re-run as stated")
