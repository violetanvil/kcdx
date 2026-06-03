# Finding — the CCryPak search-path REGISTRAR (mechanism 2 viability)

Captured 2026-06-02. Additive to `subresolver-decompiled-mechanism.md` + `FINDINGS.md`.
Trust level: PRIMARY EVIDENCE (fresh Ghidra decompile + capstone abi_walker of
WHGame.dll release_1_5_1164953_841, image base 0x180000000). Every claim cites a
decompile/disasm line. Slots binary-read from the vtable, not a header (AP3). ABI from
decompile + abi_walker, no prologue guessing (AP2). No live probe; runtime-effect items
flagged NEEDS-LIVE-CONFIRM.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
`PakSearchPathAPI.java` (`_searchpath_api_raw.txt` — full vtable + member-ref scan),
`PakAddModABI.java` (`_searchpath_addmod_abi_raw.txt` — AddMod body + helpers + xrefs),
`phase6_abi_walker.py` on 0x1819AF1A8 (`_searchpath_api_19af1a8_abi.txt`).

## The registrar — CCryPak vtable slot 19, FUN_1819af1a8, RVA 0x19AF1A8

It is canonical CryEngine **`CCryPak::AddMod`** — it push_backs a path onto the
search-path vector at `[this+0x198]` (the SAME vector the slot-1 resolver iterates).
Body (`_searchpath_addmod_abi_raw.txt` lines 58-90):

```c
void FUN_1819af1a8(CCryPak* this /*rcx*/, const char* path /*rdx*/) {
  FUN_1804628a0(strbuf, path);          // CryStringT-from-cstr ctor (rdx forwarded)
  FUN_180464788(strbuf, 0x5c, 0x2f);    // normalize '\' -> '/'
  FUN_1804d785c(strbuf);                // lowercase / trim
  for (e = [this+0x198]; e != [this+0x1a0]; e++)   // dedup scan of existing entries
    if (_stricmp(*e, strbuf) == 0) goto done;       // already registered -> no-op
  FUN_1806962e0(tmp, strbuf);           // copy-construct a CryStringT entry
  FUN_18041d3a8(this+0x198, tmp);       // PUSH_BACK onto the [+0x198] vector
  FUN_1804fdaf8(tmp - 0xc);             // release temp
done: ...
}
```

- `FUN_18041d3a8` is the vector push_back: `if (end != cap) { construct at end; end += 8; } else grow` (`_searchpath_addmod_abi_raw.txt` 106-119). `+0x198` = begin, `+0x1a0` = end, `+0x1a8` = cap.
- `FUN_1806962e0` is the CryStringT copy-ctor (`_searchpath_addmod_abi_raw.txt` 127-143) — the vector element is a **CryStringT** (heap char buffer with the `{…,nLength}` header at `-0xc`), matching the resolver's `_stricmp((char*)*entry, …)` read.
- Slot 19 has **3 DATA xrefs, ZERO direct call sites** — it is invoked only virtually (`(*(*pak+0x98))(pak, path)`). The 3 DATA xrefs are vtable slots: `0x183a96040` (= 0x183A95FA8 + 0x98 = the CCryPak vtable slot 19) + two sibling pak-class vtables (`0x1857db9cc`, `0x184837aa0`) sharing the impl.

### ABI (AP2-clean)

    void AddMod(CCryPak* this, const char* path)   // 2-arg __fastcall member

- abi_walker (`_searchpath_api_19af1a8_abi.txt`): prologue does `mov rdi,rcx` (save `this`); **rdx is never homed/clobbered** before `lea rcx,[rsp+0x30]; call FUN_1804628a0` at 0x1819AF1D4 — so entry `rdx` (arg2 = the path cstr) forwards directly into the CryStringT ctor's source. No 3rd/stack arg read. Return: void (`void` proto; the function falls to a cookie check + ret with no value set).
- **What to pass:** `pak->AddMod("<path>")` where `<path>` is a C string. The path is normalized (`\`→`/`, lowercased) and stored verbatim as a search-path ROOT. Sibling slots: **slot 20 = RemoveMod** (`FUN_18241ce08`, scan+erase `FUN_18241f740`), **slot 21 = GetMod(i)** (`FUN_18241a390`, bounds-checked `vec[i]`, count = `([+0x1a0]-[+0x198])>>3`).

## How a search-path entry is consulted — the precedence answer (Q4)

The slot-1 resolver `FUN_18046205c` (decomp 92-141; disasm 163-258) runs the search-path
loop FIRST (when `nFlags & 0x810000 == 0`, the vector is non-empty, and `path[0] != '%'`),
iterating entries **backwards** (`e = end; e != begin; e -= 8`). For each entry it builds
`<entry>/<requested-path>` and calls the leaf opener `FUN_1804621bc(this, built, out, nFlags|0x10000)`,
then tests existence per the **pakPriority cvar** (`*(*(this+0x228)+0x20)`):

| pakPriority | per-search-path-entry existence test (disasm) | loose dir resolves? |
|---|---|---|
| 0 | `FUN_1819c9cb4` DISK first (0x1821ef42a), then PAK | **YES** — disk checked |
| 1 | `FUN_1804631f0` PAK first (0x1821ef444), then DISK | YES (after pak miss) |
| **2 (published default)** | `FUN_1804631f0` **PAK ONLY** (0x180462171→0x18046217f); miss → next entry, **no disk fallback in the loop** | **NO** |
| 3 | `FUN_18241ad60` mods/-prefix gate, then PAK | NO plain disk |

On the first entry that "exists" the loop returns that resolved path (`0x18046219c`).
If no entry hits, it falls through (decomp 140) to the plain default resolution
`FUN_1804621bc(this, pName, out, nFlags)` (data-root prefixing, NOT the search-path arm).

`FUN_1804631f0` = PAK-membership (walks the loaded-pak array `[this+0xf0]/[+0x120]`,
thread-locked, binary-searches the pak directory index — `_subresolver_decomp.txt` 659-711);
it NEVER touches the loose filesystem. `FUN_1819c9cb4` = the OS disk-attribute query
(vtable +0x228 — `_subresolver_findings.md` line 33).

### The load-bearing precedence verdict

- At **pakPriority 2 (the published-game default)**, a registered loose directory does
  **NOT** resolve its loose files. The search-path loop, for each entry, checks PAK
  membership ONLY — a loose (non-pak) file in the registered dir fails `FUN_1804631f0`,
  the loop skips it, and resolution falls through to the default data-root path. So
  AddMod-of-a-loose-dir at mode 2 neither OVERRIDES paks nor ADDS loose files.
- The loose disk check (`FUN_1819c9cb4`) is reached for a search-path entry only at
  **pakPriority 0**, or when **bit 28 (`nFlags & 0x10000000`)** is set on the open
  (disasm 0x180462143 `BT EBP,0x1c; JC 0x1821ef460`→disk) — neither is the default for
  asset opens. (Distinct from the `0x10000` bit, which only forces the search-path ARM to
  run; within that arm, pakPriority still gates pak-vs-disk.)

## Mechanism 2 viability — FINDING (manager/user pick 1 vs 2)

**Mechanism 2 (search-path registration, no per-open hook) is NOT viable as a loose-file
overlay at the published default (`sys_pakPriority 2`).** A safe vtable API EXISTS and is
reachable (slot 19 AddMod, callable on `*(gEnv+0x50)` post-init), but registering a loose
directory does not make its loose files resolve at mode 2 — the search-path loop consults
PAK membership only for registered entries at that mode. AddMod is built to add a loose
MOD ROOT whose CONTENT IS PAKED (the `mods\<name>` pattern), not a loose-file directory.

For mechanism 2 to override/add LOOSE assets it would require one of (each its own
decision/cost):
- Globally setting `sys_pakPriority 0` (disk-first) — a project-wide behavior change that
  reorders ALL asset resolution, not just overlays (out of scope; a design call). Even at
  mode 0 the loose root must be a recognized/relative root the normalizer accepts.
- Packing the overlay content into a `.pak` and registering its mod dir — then
  `FUN_1804631f0` finds it at mode 2. This is "ship a pak", not "loose-file overlay".

Mechanism 1 (per-open FOpen redirect: rewrite to a `Data/`-relative path + `nFlags |= 0x10000`)
remains the loose-file path that works at mode 2 (already live-confirmed for `.dds`;
`.lua`+0x10000 retry is the residual confirm per `subresolver-decompiled-mechanism.md`).

## Confidence map

VERIFIED (decompiled/abi_walked, this build):
- Search-path registrar = CCryPak vtable slot 19 = `FUN_1819af1a8` (RVA 0x19AF1A8);
  it push_backs a CryStringT onto `[this+0x198]` after a dedup `_stricmp` scan.
- ABI `void AddMod(CCryPak* this, const char* path)` (rcx=this, rdx=path; rdx forwarded
  into the CryStringT ctor; no 3rd arg; void return).
- Siblings: slot 20 RemoveMod, slot 21 GetMod(i). Full 96-slot vtable mapped
  (`_searchpath_api_raw.txt`).
- The alias table `[this+0x1b0..0x1b8]` (walked by `FUN_180462664`) is a prefix-SUBSTITUTION
  table (`from\` → `to\`), NOT an additive search path — wrong tool for a loose overlay.
- Precedence: at pakPriority 2 a search-path entry is PAK-checked only (no loose disk);
  loose resolves for a search-path entry only at mode 0 or with `nFlags & 0x10000000`.

NEEDS-LIVE-CONFIRM (runtime facts, not in any body):
- That calling `pak->AddMod("…")` from a kcdx hook post-(gEnv+0x50)-init takes effect (the
  vector is writable from our point) — HIGH expected; a runtime probe (AddMod a known dir,
  GetMod-readback or re-resolve a name) would close it. Only relevant if the project still
  wants AddMod for a PAKED mod root.
- The exact bit any real asset open passes — whether any common asset class opens with
  `nFlags & 0x10000000` (which WOULD reach loose disk for a search-path entry at mode 2).
  Static scan of callers did not show it; a live probe over real asset loads would confirm.

## Seed-row candidate (AP18 — FLAGGED, NOT written; needs user sign-off)

IF mechanism 2 (or a future PAKED-mod-root feature) is chosen:
- **`CCryPak::AddMod`** — `FUN_1819af1a8`, RVA 0x19AF1A8, CCryPak vtable slot 19 (+0x98).
  Targets: register a search-path/mod ROOT (push_back onto `[this+0x198]`). ABI
  `void(CCryPak* this, const char* path)`. Siblings (secondary, only if directly needed):
  slot 20 RemoveMod (`FUN_18241ce08`, RVA 0x241CE08), slot 21 GetMod
  (`FUN_18241a390`, RVA 0x241A390). Sibling already seeded: FOpen (kcdx_id 131, slot 36),
  + the flagged AdjustFileName/resolver `FUN_18046205c` (RVA 0x6205C, slot 1).
