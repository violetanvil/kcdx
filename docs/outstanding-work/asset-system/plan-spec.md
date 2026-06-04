# Asset system — plan spec (shared spec for every step)

The implementation plan for kcdx's **asset system** — the full surface by which a
mod author adds, references, publishes, composes, and replaces game assets. Every
step doc leans on this spec rather than restating shared context.

This is NOT "asset replacement" alone — replacement is one capability among
several. The system lets an author: **add** new assets on systems they create,
**reference** any asset (own or another mod's) in Lua/C++, **publish** stable
names as a cross-mod contract, **compose** on top of another mod's asset pack,
and **replace** vanilla or another mod's assets — all with zero engine knowledge
and no implicit magic.

- **Goal:** ship kcdx's asset system — author drops files in one `assets/` folder;
  references any asset (own or another mod's) by path or published name through a
  navigable namespace; declares replacements explicitly (sidecar or code);
  registers/publishes assets at runtime; with transparent class-agnostic serving
  and zero engine knowledge.
- **Settled design (the authority — build to IT, not to a step's prose summary):**
  [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
  **v2** (the canonical TRD, committed `9c891b1`; changelog
  `../../design/asset-replacement-changelog.md`). Every step doc cites the specific
  `§` it builds. `.claude/rules/spec-conformance.md`: a step doc is a pointer to
  that design, never a replacement.

## The seam — TWO coordinated hooks (the v2 mechanism every Phase-1 step builds to)

The design's §7, gated against the load-path research (commit `3193e84`) + the
FOpen-handle finding (commit `e6e8e27`):

- **HOOK 1 — the resolution DECISION:** replace `CCryPak::AdjustFileName` (slot 1,
  id 152) via `hook_chain::AddCEngine`. kcdx decides which file wins, for every
  asset class and BOTH byte-lanes (loose + mount/stream), above the
  `sys_pakPriority` gate. MISS → call through the leaves (ids 153/154/155) so stock
  content (incl. a stock Nexus/Workshop pak, US-7) resolves unchanged. This reaches
  **replace-vanilla** (the pak/mount lane).
- **HOOK 2 — the loose OPEN:** on a declared-overlay hit, kcdx opens the loose file
  itself and returns its OWN CRT `FILE*` (gate-verified: FRead's OS arm serves any
  `FILE*` whose `handle−1 ≫ pak-handle-count`). kcdx never depends on the engine's
  loose-search finding the file — it owns the handle. This serves **add-new** and
  the loose side of replace.

Why two hooks (the v1 correction): every class opens via FOpen but bytes arrive on
two lanes — loose (FOpen mints a `FILE*`) and mount/stream (pak-resident read on a
mount-minted handle, NOT FOpen); the resolver is `sys_pakPriority`-gated (pak-only
at the default), so slot-1-alone can't serve loose bytes and FOpen-alone can't
replace a pak asset. Owning the decision + the open covers both. (The v1
"single-FOpen-hook / FOpen-calls-slot-1 / independent-of-sys_pakPriority" framing
was the falsified inference the AP19 gate caught.)

## Landed foundation (built — this plan builds ON these, does not rebuild them)

- **`[entrypoints].assets` parse + the load-order overlay map** (vpath → loose
  file; `NormalizeVPath`; built at discovery), `src/config.cpp` +
  `src/asset_overlay.cpp` + `src/plugin_loader.{h,cpp}` — committed `2588b33`. The
  map + its load-order conflict reporting are built; the two hooks that CONSULT/
  SERVE it are this plan's Phase 1.
- The **ctor-vs-first-read ordering probe** (P1-s1) already fired with findings
  captured (`_research/phase8.5-pak-resolver/step1-ordering-probe-finding.md` +
  the dual-marker probe in `src/asset_overlay.cpp` / `src/mod_absorb/ctor_bracket.cpp`).

NOT foundation — removed by P1-s3 (HOOK 1): the throwaway `InstallSeamAProbe()`
SEAM-A diagnostic + the dead `CCryPak::FOpen` probe hook in
`src/asset_overlay.{h,cpp}` (the FOpen hook is not the mechanism — design §7).

## Cross-step invariants (hold across every step)

- **The seam is the two hooks above** — HOOK 1 (resolver decision, id 152) + HOOK 2
  (own-`FILE*` loose open). Independent of `sys_pakPriority` (kcdx neither sets nor
  depends on it — owns the decision above the gate + the handle beneath the search).
- **Both hooks install in the already-shipping ready-bracket** (before
  `SetEvent(g_kcdxReadyEvent)`), so both are live before the engine's first asset
  read (design §8); the `ModManager_ctor`-vs-first-read ordering is P1-s1's probe.
- **Explicit declaration, never implicit path-match.** A file's presence in
  `assets/` replaces nothing; a replacement is an explicit sidecar or code
  declaration. A mistyped target fails LOUD (`anti-patterns.md` AP14), never a
  silent orphan (design §4.1).
- **Resolve game facts by name/id, never a literal** (`no-hardcoded-addresses.md`,
  AP1). `CCryPak_AdjustFileName` (id 152), `CCryPak_IsFileInPak` (153),
  `CCryPak_DoesFileExistOnDisk` (154), `CCryPak_AdjustFileName_RootPrefix` (155) —
  seed rows exist; **no new seed row this plan** (AP18). (The id-152 seed PROSE is
  falsified and corrected by P2-s7's sweep — design §10.2.)
- **Full Lua↔C++ parity** (`lua-api-surface.md`) — every author surface ships its
  Lua verb AND its `kcdxAssetInterface` C++ mirror; parity is tested.
- **Docs move with the surface** (`docs-discipline.md`) — each `kcdx.assets.*`
  verb / interface method ships its `docs/lua/` + `docs/cpp/` entry + glossary term
  + a test row in the SAME step.
- **The author never sees the asset-class distinction** — the two hooks serve every
  class the same way from `assets/`; no `<game>/Data/` staging (HOOK 2 owns the
  handle — design §4.3, the disassembler test).
- **A stock Nexus/Workshop pak loads unchanged** — HOOK 1's MISS fall-through to
  the pak-membership leaf (id 153) resolves stock pak content as today (design §3
  US-7, §7); a regression row proves it.

## Coverage map — every design element → its covering step (or explicit deferral)

`.claude/rules/spec-conformance.md` — the durable proof the plan accounts for the
whole design (v2).

| Design element | Covered by | Notes |
|---|---|---|
| §8 seam-install ordering (`ModManager_ctor` vs first read) | P1-s1 (probe) | the gating unknown; findings captured; a falsifying outcome surfaces a fork |
| §7 DirectStorage texture-arm bypass (flagged-unverified, default-off) | P1-s2 (probe) | confirm whether DS bypasses the seam for textures before the seam ships (user-chosen probe over deferral) |
| §7 HOOK 1 — resolution DECISION (replace `AdjustFileName` id 152) + call-through leaves | P1-s3 | reaches all classes + both lanes; removes the FOpen/SEAM-A residue |
| §7 HOOK 2 — loose OPEN (return kcdx's own CRT `FILE*`) | P1-s4 | serves the loose file without the engine's loose-search; installs w/ HOOK 1 |
| §4.2 declarative sidecar (`replaces` / `replaces_plugin`+`replaces_path` / `name`) | P1-s5 | feeds the overlay map; loud error on missing target (AP14) |
| §4.4 load-order conflict resolution + report line | P1-s5 | winner/suppressed shape (map's conflict reporting built, `2588b33`) |
| US-1 — replace a vanilla asset, no code | P1-s3 (decision) + P1-s4 (open) + P1-s5 (sidecar) | dominant TC case; vanilla = pak-resident, reached via HOOK 1's resolver redirect |
| US-4 — replace another mod's asset (chain) | P1-s5 (sidecar target) + P1-s3/s4 | target = name or owner+path; load-order conflict |
| US-7 — stock Nexus/Workshop pak loads unchanged | P1-s3 (MISS fall-through) + P3-s10 (regression row) | backward-compat |
| §6 navigable `kcdx.plugin.<author>.<plugin>.*` namespace (`__index` chain) | P2-s6 | the general cross-plugin primitive; US-3 enabler |
| §10.2 stale-prose sweep (dotted-`__index` prose + the falsified id-152 seed prose) | P2-s7 (own step) | TWO targets; commit-grain on its own; the seed-prose fix is an AP19/AP2 correction |
| §5 `kcdx.assets.get_by_path` (the pure-read verb) + the `kcdx.assets.*` table | P2-s8 | depends on no runtime store; ships first (§5.2 build split) |
| §5 `kcdx.assets.{get_by_name,declare,register,replace}` (the four runtime verbs) | P2-s8b | + the string-key cross-plugin form; depend on the §5.1 store |
| §5.1 runtime-store mechanism (`asset_namespace` unit, RCU snapshot, separate from the build-time map) | P2-s8b | settled 2026-06-04 (consult); the four runtime verbs build to it |
| US-2 — reference own asset by path | P2-s8 (`get_by_path`) | own = no owner prefix |
| US-3 — reference another mod's asset (navigable ns) | P2-s6 (namespace) + P2-s8 (`get_by_path` form) + P2-s8b (`get_by_name` form) | `kcdx.plugin.<a>.<p>.assets.*` |
| US-5 — publish a name as a contract | P1-s5 (`name` sidecar) + P2-s8b (`declare` + `get_by_name`) | only named assets published; no enumeration |
| US-6 — runtime register / replace | P2-s8b (`register` / `replace`) | programmatic equivalents of the sidecar; take-effect "thereafter" (§3 US-6) |
| §5 / §10.1 C++ mirror (`kcdxAssetInterface`, full parity) | P2-s9 | each BUILT Lua verb's mirror, append-only ABI; ordered after s8b (mirrors the full surface) |
| §9 per-lane runtime acceptance (HOOK 2 serves `.lua`; HOOK 1 reaches pak/mount lane for vanilla replace) | P1-s4 (live check) + P3-s10 | the static seam is gated; this is runtime acceptance |
| §9 cFn-ABI pointer-return (HOOK 2's `FILE*` through the hook chain) | P1-s4 | hook-chain mechanics, settled at build |
| §11 manipulation (texture transforms) | **DEFERRED (§11)** | reserved + NYI doc entry; heavy codec dep, use case not concrete |
| §10.1 public author guide (`docs/lua/` + `docs/cpp/` entries) | ships per-step with each surface (`docs-discipline.md`) | each verb's entry lands in its building step |

Every design element resolves to a step or an explicit deferral.

## Build-gated probes (the plan's opening unknowns — `results-driven.md`)

The seam is gated-verified statically (the two hooks, §7). Two runtime unknowns
are resolved by probe FIRST; the dependent surface is built to the result:

1. **Seam install ordering (P1-s1).** Does `ModManager_ctor` fire BEFORE the first
   overridable asset read? Settles WHERE both hooks install (design §8). Findings
   already captured; a falsifying outcome is a surfaced fork.
2. **DirectStorage bypass (P1-s2).** Does the optional DirectStorage texture path
   (`dstorage.dll`, default-off) bypass the seam? Confirm before the seam ships
   (user-chosen probe over deferral). Outcome: covered → no action; bypasses →
   surface the DS-texture gap as a fork (scope-out or own a DS seam).

The per-lane SERVE confirmations (HOOK 2 serves a `.lua`; HOOK 1's redirect reaches
the pak/mount lane for a vanilla replace) are RUNTIME ACCEPTANCES of the
gate-verified static seam — checked live at P1-s4 + P3-s10, not mechanism-unknowns.
