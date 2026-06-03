# Finding — the loose-vs-pak resolver decompiled (settles the overlay mechanism)

Captured 2026-06-02. Fresh-Ghidra decompile of the function `CCryPak::FOpen`
(kcdx_id 131) hands the path to. This is the function that decides whether a
loose plugin overlay file resolves. Raw decompiles: `_subresolver_decomp.txt`,
`_subresolver_leaves.txt`, `_subresolver_strings.txt`; producers
`third-party-ghidra/ghidra_scripts/PakSubResolverDecomp.java` + `PakResolverStrings.java`.

## The function

`FOpen` (`FUN_1804614a0`) at `0x1804615FA` does `call [rax+8]` where `rax = [r12]`,
`r12 = this` → **CCryPak vtable slot 1 (+0x8) = `FUN_18046205c`, RVA 0x0006205C**
(read live from the CCryPak vtable @ VA 0x183A95FA8). It is CryEngine's
**`CCryPak::AdjustFileName` + search-path precedence resolver** — returns the
resolved concrete path string (`char*`), which FOpen then opens. Loose-vs-pak
precedence + path-root logic live entirely here, NOT in FOpen's body.

Children followed one level: `FUN_1819c9cb4` (disk-existence check via vtable
+0x228, the OS file-attribute query), `FUN_1804631f0` (pak directory-index
binary search), `FUN_1804621bc` (the root-prefixing / AdjustFileName core).

## Q1 — `0x10000` is the minimal loose-search flag (NOT `0x10006`)

- The resolver tests `0x10000`, `0x800000` (bit 23, user-dir), `0x200000` (bit
  21, verbatim/no-root), `0x40000` (bit 18), and the `sys_pakPriority` cvar
  (read from `*(*(this+0x228)+0x20)`). It tests **neither `0x4` nor `0x2`** —
  those are FOpen-internal (fallback-arm disposition / forwarded mode),
  upstream of this function.
- So **`0x10000` alone** engages the loose/search-path arm at the resolver
  level. `0x10006` is not required by the resolver; the `0x6` lives in FOpen's
  own arm-selection. (`ModManager_ReadModOrder` id 136 uses `0x10006` because it
  goes through FOpen's arm too — but the resolver's loose-search gate is the one
  bit `0x10000`.)
- **`sys_pakPriority` gates pak-vs-disk ORDER:** mode 0 = disk-first; 1/2 =
  pak-first-then-disk; 3 = pak-only. Published-game default is 2, so the pak arm
  wins first unless `0x10000` forces the loose/search arm.

## Q1 — why `.dds` resolved at flag 0 but `.lua` did not (hypothesis (a) CONFIRMED)

The resolver is **extension-agnostic** — no `.dds`/`.lua` branch anywhere. The
divergence is the CALLERS: texture and script open sites pass different `nFlags`
and consume the result differently. At pakPriority 2 the pak arm wins first; the
texture caller/streamer tolerated the loose handle the resolver returned at its
flags, the script open fell back to the pak copy unless `0x10000` forced the
loose arm (exactly what probe U.4 added).

## Q2 — the loose-search ROOT rule: rooted at `<game>/Data/`, NOT absolute-anywhere

`FUN_1804621bc` (decomp lines ~336–354): if `(nFlags & 0x10000) == 0` and the
path is not already under a recognized root (`languages\`, `mods\`, `editor\`,
`engine\`, or the two CryStringT roots — `this[0x31]` = game **data root** at
`this+0x188`, and `this[0x4a]`), it **prepends the data-root**. If `0x10000` is
set and the path starts with `./`/`.\`, it strips the `./` (treats it as
already data-root-relative). `0x200000` → no root logic (verbatim path).
`%user%\` only on the `0x800000` user-dir branch (cosave, not assets).

→ An arbitrary `assets/`-dir **absolute** path failed because it is re-rooted
under `Data/` (or unmatched and the disk arm not consulted at pakPriority 2). A
**`Data/`-relative** path worked.

## The TWO binary-supported mechanisms for the overlay (step 3b)

1. **Per-open FOpen redirect (surgical):** on an overlay-map hit, rewrite the
   open to a `Data/`-relative path + `nFlags |= 0x10000`. Only overlaid paths
   touched; global resolution order unchanged. `.dds` confirmed; `.lua`
   retry-with-0x10000 is the residual live confirm.
2. **Search-path registration (no per-open hook):** add the plugin's overlay
   root to CCryPak's search-path vector (`this+0x198..0x1a0` — the entries the
   resolver's loop iterates) so loose files resolve by name at normal
   precedence, no per-call flag surgery. Changes GLOBAL resolution order; needs
   a probe to confirm the vector is writable from our hook point + ordering.

User decision 2026-06-02: PROBE BOTH, then decide the architecture on full
evidence (the `.lua`+0x10000 retry for mechanism 1; a search-path-registration
probe for mechanism 2).

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user approval)

- **`CCryPak::AdjustFileName` / loose-vs-pak precedence resolver** — `FUN_18046205c`,
  RVA 0x0006205C, CCryPak vtable slot 1 (+0x8). The function a path-overlay hook
  or search-path registration targets. Sibling already seeded: FOpen (id 131,
  slot 36).
- Secondary (only if the overlay needs them directly): `FUN_1819c9cb4`
  (disk-existence, vtable +0x228), `FUN_1804631f0` (pak-membership).
