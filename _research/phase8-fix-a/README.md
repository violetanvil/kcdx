# Phase 8 — FIX A symbol harvest

Recon workspace for harvesting the runtime RVAs of all 117
LUA_API + LUALIB_API functions in WHGame.dll, so kcdx can route
its Lua C API through WHGame's compiled Lua instead of the
statically-linked `vendor/lua` (eliminating the dual-Lua sentinel
hazard by construction).

See `kcdx/docs/outstanding-work/fix-a-drop-static-lua.md` for the
full design and motivation.

## Methodology

**Primary**: B + C from the discussion in the parent conversation.

- **B = string-anchor xref walks**. Functions that build error
  messages (`luaL_typerror`, `luaL_argerror`, `lua_resume`,
  `lua_dump`, etc.) reference unique strings we can find in
  WHGame.dll's `.rdata` and trace back via xrefs.
- **C = call-graph bootstrap from anchors**. We have one known
  anchor: `lua_pcall @ WHGame.dll + 0x71A5A4` (verified by AOB
  match on `48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8`,
  originally from yobson1's kcd2lua mod, re-verified on KCD2
  build release_1_5_1164953_841).

  From `lua_pcall`'s body we can identify what it calls
  (`luaD_pcall`, `lua_settop`, `f_call`, etc.) by matching their
  bytes against our compiled `vendor/lua/lapi.obj` etc.

**Cross-check**: A = relocation-aware AOB. For each function,
extract prologue bytes from our compiled .obj and mask out every
byte covered by a relocation in the .obj's relocation table.
Scan WHGame.dll's `.text` for the masked pattern. Used to
validate B/C identifications, not as the sole source of truth.

**Avoided**: D = Ghidra FidDb. The doc reports the planned
FidDb-based workflow failed (empty FidDb files on this machine).
Worth diagnosing as parallel work, but not the main line.

## Calibration

WHGame.dll PE layout (verified 2026-05-21):

- ImageBase: `0x180000000`
- `.text`:   vaddr `0x00001000`, file offset `0x400`,  size `0x3a01000` (~58 MB)
- `.rdata`:  vaddr `0x03a02000`, file offset `0x3a01400`, size `0xecf000`
- `.data`:   vaddr `0x048d1000`, file offset `0x48d0400`
- `.pdata`:  vaddr `0x05688000`, file offset `0x506b200`

File offset for an RVA = `rva - 0x1000 + 0x400` (for `.text`).
VA = `0x180000000 + rva`.

## Anchor

`lua_pcall`:
- RVA `0x71A5A4`, VA `0x18071A5A4`
- File offset `0x7199A4`
- First 16 bytes: `48 89 5C 24 08 57 48 83 EC 40 33 C0 41 8B F8 44`

This is the seed for the call-graph walk.

## Why approach A (relocation-aware AOB) is only a weak check, not primary

`lua_pcall` calibration confirms the doc's warning: WHGame is PGO-built
and its bytes differ from our local /O2 build at the *instruction* level,
not just the linker-patched-bytes level.

| build  | first 16 bytes of lua_pcall |
|--------|-----------------------------|
| ours   | `48 89 5C 24 08 57 48 83 EC 40 41 8B F8 44 8B D2` |
| WHGame | `48 89 5C 24 08 57 48 83 EC 40 33 C0 41 8B F8 44` |

Same first 10 bytes (`48 89 5C 24 08 57 48 83 EC 40` — `mov [rsp+8], rbx;
push rdi; sub rsp, 0x40`). Then divergence: ours has `41 8B F8 44 8B D2`
(`mov edi, r9d; mov edx, r10d`), WHGame has `33 C0 41 8B F8 44` (`xor
eax, eax; mov edi, r9d; mov ...`). The `33 C0` is PGO's, and it shifts
all subsequent bytes by 2.

Reloc-aware masking can't recover this — the `33 C0` isn't a linker patch,
it's a real instruction PGO emitted because the profile said `eax`
needed zeroing on entry.

So A is a weak check ("if the prologue's *unrelocated* unique bytes
match, that's evidence"), not a primary identification method. B (string
xrefs) and C (call-graph from anchors) are the load-bearing tools.

## Outputs

When this directory is populated:

- `lua_rvas.csv` — final harvested RVAs (117 rows). Format:
  `name,rva,va,confidence,evidence`. Confidence: `verified`
  (call-graph + string + AOB cross-check all agree), `high`
  (two of three agree), `medium` (one strong source, no
  contradictions), `unverified` (single weak source — do NOT
  ship).

- `coff_inspect.py` — parser for COFF .obj files (extracts
  symbol → bytes + relocations). Used by A.

- `aob_scan.py` — relocation-aware AOB scanner. Reads
  WHGame.dll's `.text`, takes a (bytes, relocation-mask) pattern,
  returns RVAs of all matches.

- `callgraph_walk.py` — given a known anchor RVA, disassemble
  its body and identify direct calls (`E8` instructions);
  resolve callee RVAs from the rel32 offset.

- `string_xrefs.py` — given a string, find it in `.rdata` and
  walk xrefs back to enclosing functions.

- `notes/` — per-function evidence trails. One markdown file per
  function explaining why we believe its RVA is correct.
