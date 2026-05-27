#!/usr/bin/env python3
"""Harvest WHGame.dll RVAs for every lua_* / luaL_* function kcdx uses.

Strategy:
  1. Parse each vendor/lua/<n>.obj COFF file from build/lua.dir/Release/.
     Extract every lua_* / luaL_* external symbol's section + offset, and
     the section's raw bytes. The first ~24 bytes of each function = its
     prologue.
  2. Generate an AOB pattern from each prologue. Wildcard rip-relative
     operands (E8/E9 call/jmp displacements, 0F 8x conditional jumps,
     mov [rip+disp]/lea [rip+disp]) since those WILL shift between our
     /O2 build and WHGame's /O2+PGO build, but the surrounding opcodes
     stay stable.
  3. AOB-scan WHGame.dll's .text section for each pattern. Emit
     (name, rva, n_matches) CSV. Multi-match entries flagged for review.

Output: _research/phase7-recon/lua-rvas-harvested.csv

Run from the kcdx repo root:
    python _research/phase7-recon/harvest_lua_rvas.py
"""

import os
import re
import struct
import sys
from pathlib import Path

import pefile


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJ_DIR = REPO_ROOT / "build" / "lua.dir" / "Release"
WHGAME_DLL = Path(
    "E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2"
    "/Bin/Win64MasterMasterSteamPGO/WHGame.dll"
)
OUTPUT_CSV = REPO_ROOT / "_research" / "phase7-recon" / "lua-rvas-harvested.csv"
PROLOGUE_BYTES = 24
TARGET_PREFIXES = ("lua_", "luaL_", "luaopen_")


# ---------------------------------------------------------------------------
# COFF .obj parser (minimal — only what we need)
# ---------------------------------------------------------------------------

class CoffObj:
    """Parse a Microsoft COFF .obj file. Just enough to:
       - Enumerate section names + their raw bytes.
       - Enumerate external function symbols + their (section, offset)."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self._parse()

    def _parse(self):
        # COFF file header: 20 bytes
        # https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
        (machine, n_sections, time_stamp, sym_ptr,
         n_syms, opt_hdr_size, characteristics) = \
            struct.unpack_from("<HHIIIHH", self.data, 0)
        self.n_sections = n_sections
        self.sym_table_offset = sym_ptr
        self.n_syms = n_syms

        # Section headers: 40 bytes each, immediately after file header +
        # optional header (.obj has no optional header).
        sh_offset = 20 + opt_hdr_size
        self.sections = []
        for i in range(n_sections):
            o = sh_offset + 40 * i
            name = self.data[o : o + 8].rstrip(b"\x00").decode("ascii", "replace")
            virt_size = struct.unpack_from("<I", self.data, o + 8)[0]
            virt_addr = struct.unpack_from("<I", self.data, o + 12)[0]
            raw_size = struct.unpack_from("<I", self.data, o + 16)[0]
            raw_ptr = struct.unpack_from("<I", self.data, o + 20)[0]
            chars = struct.unpack_from("<I", self.data, o + 36)[0]
            raw_bytes = self.data[raw_ptr : raw_ptr + raw_size] if raw_size else b""
            self.sections.append({
                "name": name, "raw_size": raw_size,
                "characteristics": chars, "bytes": raw_bytes,
            })

        # String table: comes after the symbol table. First 4 bytes = total
        # size (including the 4 bytes themselves).
        st_start = sym_ptr + 18 * n_syms
        st_size = struct.unpack_from("<I", self.data, st_start)[0]
        self.string_table = self.data[st_start : st_start + st_size]

        # Symbol table: 18 bytes per entry. Long names (>8 chars) live in
        # the string table (first 4 bytes of the name field = 0, next 4 =
        # offset into string table).
        self.symbols = []
        i = 0
        while i < n_syms:
            o = sym_ptr + 18 * i
            name_field = self.data[o : o + 8]
            if name_field[:4] == b"\x00\x00\x00\x00":
                strtab_off = struct.unpack_from("<I", name_field, 4)[0]
                end = self.string_table.find(b"\x00", strtab_off)
                if end < 0:
                    end = len(self.string_table)
                name = self.string_table[strtab_off:end].decode("ascii", "replace")
            else:
                name = name_field.rstrip(b"\x00").decode("ascii", "replace")

            value = struct.unpack_from("<I", self.data, o + 8)[0]
            section_num = struct.unpack_from("<h", self.data, o + 12)[0]
            sym_type = struct.unpack_from("<H", self.data, o + 14)[0]
            storage = struct.unpack_from("<B", self.data, o + 16)[0]
            n_aux = struct.unpack_from("<B", self.data, o + 17)[0]

            self.symbols.append({
                "name": name, "value": value, "section": section_num,
                "type": sym_type, "storage": storage,
            })

            i += 1 + n_aux  # skip aux records


# ---------------------------------------------------------------------------
# Prologue extraction
# ---------------------------------------------------------------------------

def extract_prologues():
    """Returns dict {func_name: prologue_bytes} for every lua_*/luaL_*
    external symbol across all .obj files."""
    prologues = {}
    for obj_path in sorted(OBJ_DIR.glob("*.obj")):
        try:
            obj = CoffObj(obj_path)
        except Exception as e:
            print(f"!! failed to parse {obj_path.name}: {e}", file=sys.stderr)
            continue

        for sym in obj.symbols:
            if sym["storage"] != 2:  # IMAGE_SYM_CLASS_EXTERNAL
                continue
            if sym["section"] <= 0:
                continue  # undefined / absolute / debug
            if not sym["name"].startswith(TARGET_PREFIXES):
                continue

            sec_idx = sym["section"] - 1
            if sec_idx >= len(obj.sections):
                continue
            section = obj.sections[sec_idx]
            # Only code sections (IMAGE_SCN_CNT_CODE = 0x00000020 + EXECUTE)
            if not (section["characteristics"] & 0x20):
                continue
            offset = sym["value"]
            end = min(offset + PROLOGUE_BYTES, len(section["bytes"]))
            body = section["bytes"][offset:end]
            if len(body) < PROLOGUE_BYTES:
                continue  # truncated, useless

            # Strip leading int3 padding (linker pads small functions)
            while body and body[0] == 0xCC:
                body = body[1:]
            if len(body) < 16:
                continue

            # If we've seen this symbol already (multiple .obj's might
            # define static helpers with the same name), keep the longer
            # prologue. Functions exposed via LUA_API are non-static so
            # this dedupes accidentally.
            existing = prologues.get(sym["name"])
            if not existing or len(body) > len(existing):
                prologues[sym["name"]] = body
    return prologues


# ---------------------------------------------------------------------------
# AOB pattern generation
# ---------------------------------------------------------------------------

# We can't trivially identify rip-relative operands from raw bytes without
# disassembling. capstone is available, use it.
import capstone


def make_aob_pattern(prologue):
    """Disassemble prologue. For each instruction, copy the opcode bytes
    verbatim but wildcard any rip-relative operand bytes (4 bytes per E8/E9
    call/jmp displacement, 4 bytes per `??/0F 80..8F xx xx xx xx`, 4 bytes
    per `48 8B 05/0D/15/1D xx xx xx xx mov r,[rip+disp]`, lea similar)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    mask = []  # parallel to prologue bytes: True = wildcard
    for ins in md.disasm(prologue, 0x1000_0000_0000_0000):
        ins_start = ins.address - 0x1000_0000_0000_0000
        ins_end = ins_start + ins.size
        if ins_start >= len(prologue):
            break
        for i in range(ins_start, min(ins_end, len(prologue))):
            mask.append(False)
        # Identify rip-relative operands in this instruction
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                # The displacement is the last 4 bytes of the instruction
                for j in range(max(ins_start, ins_end - 4), ins_end):
                    if j < len(mask):
                        mask[j] = True
            if op.type == capstone.x86.X86_OP_IMM and ins.mnemonic in ("call", "jmp",
                                                                       "je", "jne", "jz", "jnz",
                                                                       "jg", "jge", "jl", "jle",
                                                                       "ja", "jae", "jb", "jbe",
                                                                       "js", "jns", "jo", "jno",
                                                                       "jp", "jnp", "jc", "jnc"):
                # CALL/Jcc: opcode is 1 byte (E8/E9) or 2 bytes (0F 8x).
                # Displacement is the LAST 4 bytes (32-bit rel) or LAST
                # 1 byte (8-bit rel for short jumps — but those don't shift
                # across builds the same way; skip wildcarding them).
                # Detect by ins.size: rel32 ⇒ size ≥ 5; rel8 ⇒ size = 2.
                if ins.size >= 5:
                    for j in range(max(ins_start, ins_end - 4), ins_end):
                        if j < len(mask):
                            mask[j] = True

    if len(mask) < len(prologue):
        # Disassembly failed past some point; truncate prologue accordingly
        prologue = prologue[: len(mask)]

    pattern_bytes = []
    for i, b in enumerate(prologue):
        pattern_bytes.append("??" if mask[i] else f"{b:02X}")
    return " ".join(pattern_bytes)


# ---------------------------------------------------------------------------
# AOB scan in WHGame.dll
# ---------------------------------------------------------------------------

def aob_scan(haystack, pattern):
    """Find all offsets in haystack matching the AOB pattern string.

    Implemented via re.compile + bytes regex so the scan runs in native
    code instead of Python's interpreter loop. ~1000x faster than a
    naive Python double loop for our pattern sizes."""
    import re as _re
    parts = pattern.split()
    if not parts:
        return []
    pat_bytes = bytearray()
    for p in parts:
        if p == "??":
            pat_bytes += b"."  # any byte
        else:
            b = int(p, 16)
            # Literal byte — escape if it would be a regex metachar
            pat_bytes += _re.escape(bytes([b]))
    rx = _re.compile(bytes(pat_bytes), _re.DOTALL)
    hits = []
    for m in rx.finditer(haystack):
        hits.append(m.start())
        if len(hits) > 8:
            break
    return hits


def scan_whgame(prologues):
    """Returns dict {func_name: [whgame_rva, ...]}."""
    pe = pefile.PE(str(WHGAME_DLL), fast_load=True)
    text_section = None
    for sect in pe.sections:
        name = sect.Name.rstrip(b"\x00").decode("ascii", "replace")
        if name == ".text":
            text_section = sect
            break
    if not text_section:
        raise RuntimeError(".text section not found in WHGame.dll")
    text_bytes = text_section.get_data()
    text_rva_base = text_section.VirtualAddress

    print(f"WHGame.dll .text @ RVA 0x{text_rva_base:X}, size {len(text_bytes):,} bytes")

    results = {}
    for name, prologue in sorted(prologues.items()):
        pattern = make_aob_pattern(prologue)
        offsets = aob_scan(text_bytes, pattern)
        rvas = [text_rva_base + off for off in offsets]
        results[name] = rvas
    return results


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if not WHGAME_DLL.exists():
        print(f"!! WHGame.dll not found at {WHGAME_DLL}", file=sys.stderr)
        sys.exit(1)
    if not OBJ_DIR.exists():
        print(f"!! Lua .obj dir not found at {OBJ_DIR}", file=sys.stderr)
        print("   Run kcdx build first: pwsh ./build.ps1", file=sys.stderr)
        sys.exit(1)

    print(f"Extracting prologues from {OBJ_DIR}...")
    prologues = extract_prologues()
    print(f"Got {len(prologues)} unique lua_*/luaL_* prologues")

    print(f"Scanning {WHGAME_DLL}...")
    results = scan_whgame(prologues)

    OUTPUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_CSV, "w", newline="") as f:
        f.write("name,rva,n_matches,prologue_pattern\n")
        n_unique = 0
        n_ambiguous = 0
        n_missing = 0
        for name in sorted(results):
            rvas = results[name]
            pattern = make_aob_pattern(prologues[name])
            if not rvas:
                f.write(f"{name},,0,{pattern}\n")
                n_missing += 1
            elif len(rvas) == 1:
                f.write(f"{name},0x{rvas[0]:08X},1,{pattern}\n")
                n_unique += 1
            else:
                rva_list = "|".join(f"0x{r:08X}" for r in rvas)
                f.write(f"{name},{rva_list},{len(rvas)},{pattern}\n")
                n_ambiguous += 1

    print(f"\nResults written to {OUTPUT_CSV}")
    print(f"  {n_unique:>4} unique  (one match each, ready to use)")
    print(f"  {n_ambiguous:>4} ambiguous (multiple matches; need refinement)")
    print(f"  {n_missing:>4} missing  (no match; PGO probably reorganized)")
    print(f"  {len(prologues):>4} total prologues attempted")


if __name__ == "__main__":
    main()
