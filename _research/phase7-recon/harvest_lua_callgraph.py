#!/usr/bin/env python3
"""Harvest WHGame.dll Lua function RVAs via call-graph bootstrap.

Strategy:
  1. Parse every .obj file in build/lua.dir/Release/. For each function
     symbol, record its in-source order of REL32-relocated CALLs to
     other functions. This is the "local call graph."
  2. Start from a known anchor: lua_pcall at WHGame.dll+0x71A5A4 (verified
     in the address library seed). Disassemble its body with capstone,
     collect every CALL instruction (in instruction order) and its
     resolved absolute target.
  3. Cross-reference: WHGame's lua_pcall makes the same sequence of CALLs
     as our local lua_pcall (in source order). So WHGame_lua_pcall's
     i-th CALL targets the function our local lua_pcall's i-th CALL
     targets. That gives us the name -> RVA mapping for every callee.
  4. Recurse: each newly-identified function becomes a new anchor; walk
     its CALLs the same way.
  5. Handle skews (PGO inlining, divergent optimizer choices) by trying
     each candidate match and preferring the one whose own callee count
     also matches in the next step. If we can't reconcile, skip and log.

Outputs: _research/phase7-recon/lua-rvas-callgraph.csv with
  name,rva,provenance,n_callees_local,n_callees_whgame

Run from the kcdx repo root:
    python _research/phase7-recon/harvest_lua_callgraph.py
"""

import struct
import sys
from collections import deque
from pathlib import Path

import capstone
import pefile


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJ_DIR = REPO_ROOT / "build" / "lua.dir" / "Release"
WHGAME_DLL = Path(
    "E:/SteamLibrary/steamapps/common/KingdomComeDeliverance2"
    "/Bin/Win64MasterMasterSteamPGO/WHGame.dll"
)
OUTPUT_CSV = REPO_ROOT / "_research" / "phase7-recon" / "lua-rvas-callgraph.csv"

# The verified anchor — lua_pcall at WHGame.dll RVA 0x71A5A4
# (from _research/phase7-recon/address-library-seed.csv id=1000).
ANCHOR_NAME = "lua_pcall"
ANCHOR_RVA = 0x0071A5A4

# Additional seed anchors from harvest_lua_rvas.py's unique-match output.
# These are functions whose AOB prologue uniquely identified them in
# WHGame.dll. We trust them as starting points and propagate from each
# in addition to lua_pcall.
SEED_CSV = REPO_ROOT / "_research" / "phase7-recon" / "lua-rvas-harvested.csv"


# ---------------------------------------------------------------------------
# COFF .obj parser (extended from harvest_lua_rvas.py — also reads
# section relocations to enumerate REL32 references).
# ---------------------------------------------------------------------------

class CoffObj:
    """Parse a Microsoft COFF .obj file. Extracts:
       - Sections (name, raw bytes, n_relocs, reloc_table_offset)
       - Symbols (name, value, section, storage)
       - Per-section relocations: list of (offset_in_section, sym_idx, type)
    """

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self._parse()

    def _parse(self):
        # COFF file header: 20 bytes
        (machine, n_sections, time_stamp, sym_ptr,
         n_syms, opt_hdr_size, characteristics) = \
            struct.unpack_from("<HHIIIHH", self.data, 0)
        self.sym_table_offset = sym_ptr
        self.n_syms = n_syms

        # Section headers: 40 bytes each
        sh_offset = 20 + opt_hdr_size
        self.sections = []
        for i in range(n_sections):
            o = sh_offset + 40 * i
            name = self.data[o : o + 8].rstrip(b"\x00").decode("ascii", "replace")
            virt_size = struct.unpack_from("<I", self.data, o + 8)[0]
            raw_size = struct.unpack_from("<I", self.data, o + 16)[0]
            raw_ptr = struct.unpack_from("<I", self.data, o + 20)[0]
            reloc_ptr = struct.unpack_from("<I", self.data, o + 24)[0]
            n_relocs = struct.unpack_from("<H", self.data, o + 32)[0]
            chars = struct.unpack_from("<I", self.data, o + 36)[0]
            raw_bytes = self.data[raw_ptr : raw_ptr + raw_size] if raw_size else b""

            # Read relocations: 10 bytes each
            relocs = []
            for j in range(n_relocs):
                ro = reloc_ptr + 10 * j
                r_va = struct.unpack_from("<I", self.data, ro)[0]
                r_sym = struct.unpack_from("<I", self.data, ro + 4)[0]
                r_type = struct.unpack_from("<H", self.data, ro + 8)[0]
                relocs.append((r_va, r_sym, r_type))

            self.sections.append({
                "name": name, "raw_size": raw_size,
                "characteristics": chars, "bytes": raw_bytes,
                "relocs": relocs,
            })

        # String table
        st_start = sym_ptr + 18 * n_syms
        if st_start + 4 <= len(self.data):
            st_size = struct.unpack_from("<I", self.data, st_start)[0]
            self.string_table = self.data[st_start : st_start + st_size]
        else:
            self.string_table = b""

        # Symbol table — also keep raw index so relocations can resolve
        self.symbols = []
        self.symbol_by_index = {}
        i = 0
        idx = 0
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

            sym = {"name": name, "value": value, "section": section_num,
                   "type": sym_type, "storage": storage, "index": i}
            self.symbols.append(sym)
            self.symbol_by_index[i] = sym
            i += 1 + n_aux


# ---------------------------------------------------------------------------
# Build local call graph
# ---------------------------------------------------------------------------

# IMAGE_REL_AMD64_REL32 = 0x0004 (also 0x0005..0x0008 = REL32_1..REL32_5)
REL32_TYPES = {0x0004, 0x0005, 0x0006, 0x0007, 0x0008}

# Lua internal prefixes — all symbols from vendor/lua/'s code that we want
# to follow in the call graph. Includes both the public API (lua_, luaL_,
# luaopen_) and the internal namespaces (luaC_, luaD_, etc.). We also
# include the empty-prefix STATIC helpers below by including any symbol
# defined in a Lua .obj file. See _is_lua_internal_name.
_LUA_PREFIXES = (
    "lua_", "luaL_", "luaopen_",
    "luaC_", "luaD_", "luaF_", "luaG_", "luaH_", "luaI_",
    "luaK_", "luaM_", "luaO_", "luaP_", "luaS_", "luaT_",
    "luaU_", "luaV_", "luaX_", "luaY_", "luaZ_",
)


def _is_lua_internal_name(name):
    """True if name is from the Lua source family (public or internal).
    Static helpers without lua-prefix are added separately by the caller
    when seen in a Lua .obj file."""
    return name.startswith(_LUA_PREFIXES)


def build_local_call_graph():
    """Returns: {func_name: [callee_name_in_source_order, ...]}
    Only includes External-class symbols (e.g., LUA_API / LUALIB_API
    functions). Static helpers are skipped (they have storage=3 STATIC)
    because they won't be visible in WHGame anyway and adding them
    creates confusion."""
    graph = {}
    func_section_map = {}  # (obj_path, section_idx) -> primary func name
    for obj_path in sorted(OBJ_DIR.glob("*.obj")):
        try:
            obj = CoffObj(obj_path)
        except Exception as e:
            print(f"!! {obj_path.name}: {e}", file=sys.stderr)
            continue

        # Identify functions: External (storage=2) OR Static (storage=3)
        # symbols pointing to a code section. For each, collect all CALL
        # relocations (REL32) within that function's bytes.
        #
        # Functions are typically each in their own SECT (the linker
        # COMDAT-folds them later). So per-function relocations = the
        # section's relocations.
        for sym in obj.symbols:
            if sym["storage"] not in (2, 3):
                continue
            if sym["section"] <= 0:
                continue
            sec_idx = sym["section"] - 1
            if sec_idx >= len(obj.sections):
                continue
            section = obj.sections[sec_idx]
            if not (section["characteristics"] & 0x20):
                continue  # not code

            name = sym["name"]
            # Include every function defined in a Lua .obj file: public
            # API (lua_*, luaL_*), internal namespaces (luaC_, luaD_,
            # luaF_, etc.), AND file-local static helpers
            # (index2adr, f_call, etc.). Filter out names that are
            # clearly not from Lua (compiler internals, CRT).
            if name.startswith("$") or name.startswith("?"):
                continue
            if name.startswith("__"):
                continue  # CRT/compiler intrinsic stubs

            # Each section in /Gy mode contains exactly one function. The
            # CALL relocations in that section are the callees of this
            # function, in source order. We filter to relocations whose
            # TARGET is also in a code section, which eliminates LEAs
            # to data symbols (luaO_nilobject_, string literals, etc.).
            callees = []
            for r_va, r_sym, r_type in section["relocs"]:
                if r_type not in REL32_TYPES:
                    continue
                target = obj.symbol_by_index.get(r_sym)
                if not target:
                    continue
                target_name = target["name"]
                # Filter compiler-internal symbols first
                if target_name.startswith("??"):
                    continue  # string literal mangling
                if target_name.startswith("$"):
                    continue
                if target_name.startswith("__"):
                    continue  # CRT intrinsic stub
                if target_name == "__ImageBase":
                    continue
                # Now check target is in a CODE section. The target
                # symbol's section field tells us; we look up the section
                # in the same .obj.
                target_sec_idx = target["section"]
                if target_sec_idx <= 0:
                    # Undefined (external import) — could be from another
                    # .obj. Trust it for now; CRT imports would have
                    # already been filtered above.
                    pass
                else:
                    target_sec = obj.sections[target_sec_idx - 1]
                    if not (target_sec["characteristics"] & 0x20):
                        continue  # data section — not a callable
                # Adjust r_va to be the offset of the CALL/JMP instruction
                # itself (not the rel32 displacement field). MSVC's REL32
                # relocation has r_va pointing at the start of the 4-byte
                # displacement; the instruction byte (E8/E9) is at r_va-1
                # for direct calls. Most REL32 relocations are 4 bytes
                # back from the next instruction; for our purposes the
                # relative ordering is what matters, not the exact byte.
                callees.append((r_va, target_name))
            # Sort by va so source order is preserved. Keep both (offset, name)
            callees.sort(key=lambda x: x[0])
            graph[name] = callees  # list of (offset_in_section, name)

    return graph


# ---------------------------------------------------------------------------
# WHGame disassembly + call enumeration
# ---------------------------------------------------------------------------

class WhgameView:
    def __init__(self, dll_path):
        self.pe = pefile.PE(str(dll_path), fast_load=True)
        self.text_section = None
        for sect in self.pe.sections:
            name = sect.Name.rstrip(b"\x00").decode("ascii", "replace")
            if name == ".text":
                self.text_section = sect
                break
        if not self.text_section:
            raise RuntimeError(".text section not found")
        self.text_bytes = self.text_section.get_data()
        self.text_rva = self.text_section.VirtualAddress
        self.text_size = len(self.text_bytes)
        self.image_base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def read_bytes(self, rva, n):
        off = rva - self.text_rva
        if off < 0 or off + n > self.text_size:
            return b""
        return self.text_bytes[off : off + n]

    def disasm_function(self, entry_rva, max_bytes=4096):
        """Disassemble starting at entry_rva. Determines function end by
        tracking the highest reachable forward jump target seen so far:
        we're past the end of the function when we're (a) past a RET/JMP
        with no remaining jump targets ahead, AND (b) the next byte is
        INT3 padding (0xCC) or NOP-aligned (0x90).
        """
        buf = self.read_bytes(entry_rva, max_bytes)
        if not buf:
            return []
        instrs = []
        max_target_off = 0  # max forward jump target relative to entry
        for ins in self.md.disasm(buf, entry_rva + self.image_base):
            instrs.append(ins)
            off_in_func = ins.address - self.image_base - entry_rva
            end_of_this_ins = off_in_func + ins.size
            mnem = ins.mnemonic.upper()

            # Track forward branch targets within the same function.
            if mnem in ("CALL", "JMP") or mnem.startswith("J"):
                # Direct-target operand?
                if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                    tgt_va = ins.operands[0].imm
                    tgt_off = tgt_va - self.image_base - entry_rva
                    # Only consider FORWARD jumps within reasonable range
                    # (a tail-call JMP into another function lands far
                    # away; we don't want that to extend our boundary).
                    if 0 < tgt_off < max_bytes and mnem != "CALL":
                        if tgt_off > max_target_off:
                            max_target_off = tgt_off

            # Terminator: RET / unconditional JMP / UD2.
            terminator = mnem in ("RET", "RETN", "UD2", "INT3")
            if mnem == "JMP" and ins.operands:
                # An unconditional JMP terminates ONLY if it's not a
                # forward branch within this function.
                if ins.operands[0].type == capstone.x86.X86_OP_IMM:
                    tgt_va = ins.operands[0].imm
                    tgt_off = tgt_va - self.image_base - entry_rva
                    if not (0 <= tgt_off < max_bytes):
                        terminator = True

            if terminator:
                # If we still have forward targets ahead of us, keep
                # disassembling (we're past one path's exit but the
                # function has more branches).
                if end_of_this_ins > max_target_off:
                    # Check next byte: padding => function ends here.
                    next_off = ins.address - self.image_base + ins.size
                    section_off = next_off - self.text_rva
                    if 0 <= section_off < self.text_size:
                        next_byte = self.text_bytes[section_off]
                        if next_byte in (0xCC, 0x90):
                            break
                # else: continue disassembling

        return instrs

    def find_call_targets(self, entry_rva):
        """Returns: list of (call_offset_within_func, target_rva) in
        instruction order. Only direct CALL/JMP with rel32 displacement
        (E8 / E9). Indirect calls (FF /2, FF /4) are skipped because
        they don't tell us a name."""
        instrs = self.disasm_function(entry_rva)
        targets = []
        for ins in instrs:
            mnem = ins.mnemonic.upper()
            if mnem not in ("CALL", "JMP"):
                continue
            # Only direct rel32: starts with E8 (call) or E9 (jmp)
            if len(ins.bytes) < 5:
                continue
            if ins.bytes[0] not in (0xE8, 0xE9):
                continue
            # Operand 0 is the target absolute address
            if not ins.operands:
                continue
            target_va = ins.operands[0].imm
            target_rva = target_va - self.image_base
            offset_in_func = ins.address - self.image_base - entry_rva
            targets.append((offset_in_func, target_rva, mnem))
        return targets


# ---------------------------------------------------------------------------
# Bootstrap propagation
# ---------------------------------------------------------------------------

def load_unique_seed_rvas():
    """Returns {name: rva} from harvest_lua_rvas.py's CSV — only the
    entries with n_matches=1. These are trustworthy starting anchors."""
    seeds = {}
    if not SEED_CSV.exists():
        return seeds
    with open(SEED_CSV) as f:
        f.readline()  # header
        for line in f:
            parts = line.strip().split(",", 3)
            if len(parts) < 3:
                continue
            name, rva_str, n_str = parts[0], parts[1], parts[2]
            if n_str != "1":
                continue
            if not rva_str.startswith("0x"):
                continue
            seeds[name] = int(rva_str, 16)
    return seeds


def propagate(local_graph, whgame):
    """Walk the call graph starting from ANCHOR_NAME at ANCHOR_RVA + every
    unique-match anchor from harvest_lua_rvas.py.
    Returns: ({name: rva}, [(name, reason), ...])."""
    resolved = {ANCHOR_NAME: ANCHOR_RVA}
    queue = deque([ANCHOR_NAME])

    # Load unique-match seeds and add them to both resolved and queue.
    seeds = load_unique_seed_rvas()
    for n, r in seeds.items():
        if n not in resolved:
            resolved[n] = r
            queue.append(n)
    print(f"  Added {len(seeds)} unique-match seeds from prior harvester")

    skipped = []  # (name, reason)
    while queue:
        name = queue.popleft()
        rva = resolved[name]
        local_callees = local_graph.get(name)
        if local_callees is None:
            continue  # no .obj data for this name

        whgame_targets = whgame.find_call_targets(rva)

        # Strict exact-match alignment: only propagate when local and
        # WHGame have the same number of call sites in the same function.
        # This is conservative — it skips functions that WHGame inlined —
        # but ensures we never propagate wrong names.
        #
        # Note: this requires our local /O2 build's call sequence to
        # exactly match WHGame's /O2+PGO call sequence for the function.
        # Inlining either way breaks the count. We accept the lost
        # functions; multi-seed and re-run handles partial coverage.
        n_local = len(local_callees)
        n_whgame = len(whgame_targets)

        if n_local != n_whgame:
            skipped.append((name, f"call-count mismatch local={n_local} whgame={n_whgame}"))
            continue
        if n_local == 0:
            continue  # leaf function, nothing to propagate

        # 1:1 alignment in source order.
        for w_idx, (off, target_rva, mnem) in enumerate(whgame_targets):
            callee_name = local_callees[w_idx][1]
            existing = resolved.get(callee_name)
            if existing is not None:
                if existing != target_rva:
                    # Two RVAs for same name. Most common cause: WHGame
                    # linker COMDAT-folded one function into another
                    # (one's bytes are a prefix or alias of the other).
                    # Or: source order skewed between local and WHGame
                    # for THIS function so we walked into the wrong
                    # callee.
                    skipped.append((callee_name, f"address mismatch existing=0x{existing:X} new=0x{target_rva:X} (from {name})"))
                continue
            resolved[callee_name] = target_rva
            queue.append(callee_name)

    return resolved, skipped


def main():
    print("Building local call graph from .obj files...")
    local_graph = build_local_call_graph()
    print(f"  {len(local_graph)} local lua_/luaL_ functions in .obj files")

    print(f"Loading {WHGAME_DLL}...")
    whgame = WhgameView(WHGAME_DLL)
    print(f"  .text @ RVA 0x{whgame.text_rva:X}, size {whgame.text_size:,} bytes")
    print(f"  image_base = 0x{whgame.image_base:X}")

    print(f"Bootstrap propagation from {ANCHOR_NAME} @ RVA 0x{ANCHOR_RVA:X}...")
    resolved, skipped = propagate(local_graph, whgame)

    print(f"\nResolved: {len(resolved)} functions")
    print(f"Skipped:  {len(skipped)} skew sites")

    OUTPUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_CSV, "w", newline="") as f:
        f.write("name,rva,provenance,n_callees_local,n_callees_whgame\n")
        for name in sorted(resolved):
            rva = resolved[name]
            local_n = len(local_graph.get(name, []))
            whgame_n = len(whgame.find_call_targets(rva))
            prov = "anchor" if name == ANCHOR_NAME else "callgraph"
            f.write(f"{name},0x{rva:08X},{prov},{local_n},{whgame_n}\n")
    print(f"\nWrote {OUTPUT_CSV}")

    # Also write a skipped log
    skip_path = OUTPUT_CSV.parent / "lua-rvas-callgraph-skipped.txt"
    with open(skip_path, "w") as f:
        for name, reason in skipped:
            f.write(f"{name}\t{reason}\n")
    print(f"Wrote {skip_path}")


if __name__ == "__main__":
    main()
