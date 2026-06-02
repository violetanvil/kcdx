# Asset replacement — plan spec (shared spec for every step)

The implementation plan for the settled asset-replacement design. Every step doc
leans on this spec rather than restating shared context.

- **Goal:** ship kcdx's asset-replacement surface — author drops files in one
  `assets/` folder, declares replacements explicitly (sidecar or code), references
  any asset (own or another mod's) by path or published name through a navigable
  namespace, with transparent per-class staging and zero engine knowledge.
- **Settled design (the authority — build to IT, not to a step's prose summary):**
  [`../restructure/phase-08.5-asset-replacement/asset-design.md`](../restructure/phase-08.5-asset-replacement/asset-design.md)
  (`§`-structured TRD, committed `9bb4bb1`). Every step doc cites the specific
  `§` it builds. `.claude/rules/spec-conformance.md`: a step doc is a pointer to
  that design, never a replacement.

## Landed foundation (built — this plan builds ON these, does not rebuild them)

Two steps of the design's mechanism are already built + live-verified; this plan
consumes them:

- **Production `CCryPak::FOpen` overlay hook** through the conflict engine
  (`hook_chain::AddCEngine`, pass-through body), `src/asset_overlay.{h,cpp}` —
  committed `9e524ae`.
- **`[entrypoints].assets` parse + the load-order overlay map** (vpath → loose
  file, `NormalizeVPath`, built at discovery), `src/config.cpp` +
  `src/asset_overlay.cpp` + `src/plugin_loader.{h,cpp}` — committed `2588b33`.

(These were authored as steps 1–2 in the restructure phase subdir; this standalone
plan picks up from there. The FOpen hook body is currently pass-through — Phase 1
below fills it.)

## Cross-step invariants (hold across every step)

- **Explicit declaration, never implicit path-match.** A file's presence in
  `assets/` does NOT replace anything; a replacement is an explicit sidecar or
  code declaration. A mistyped target fails LOUD (`anti-patterns.md` AP14), never
  a silent orphan (design §4.1).
- **The FOpen resolver hook is the mechanism** — the published game is paks-only
  (`sys_pakPriority 2`); search-path registration (`AddMod`) is NOT viable for
  loose overlays (design §7). Every overlay routes through the per-open redirect.
- **Resolve game facts by name/id, never a literal** (`no-hardcoded-addresses.md`,
  AP1). `CCryPak_FOpen` (id 131), `gEnv_pCryPak` (id 132),
  `CCryPak_AdjustFileName` (slot 1) — seed rows exist; no new seed row this plan
  (AP18). The `0x10000` loose-search flag + `<game>/Data/` root are design §7.
- **Full Lua↔C++ parity** (`lua-api-surface.md`) — every author surface ships its
  Lua verb AND its `kcdxAssetInterface` C++ mirror; parity is tested.
- **Docs move with the surface** (`docs-discipline.md`) — each `kcdx.assets.*`
  verb / interface method ships its `docs/lua/` + `docs/cpp/` entry + glossary
  term + a test row in the SAME step.
- **The author never sees the asset-class distinction** (memory-mapped vs
  handle-consumed) — the engine stages transparently (design §4.3, the
  disassembler test).

## Coverage map — every design element → its covering step (or explicit deferral)

`.claude/rules/spec-conformance.md` — the durable proof the plan accounts for the
whole design. A later `/execute` / review reads this to verify every element shipped.

| Design element | Covered by | Notes |
|---|---|---|
| US-1 — replace a vanilla asset, no code (declarative) | P1-s2 (resolution) + P1-s3 (sidecar parse) | the dominant TC case; memory-mapped live-verified |
| US-2 — reference own asset by path (`get_by_path`) | P2-s6 (Lua surface) | own = no owner prefix |
| US-3 — reference another mod's asset (navigable ns) | P2-s5 (namespace) + P2-s6 (surface) | `kcdx.plugin.<a>.<p>.assets.*` |
| US-4 — replace another mod's asset (chain) | P1-s3 (sidecar target) + P1-s2 (resolution) | target = name or owner+path; load-order conflict |
| US-5 — publish a name as a contract | P1-s3 (`[names]` sidecar) + P2-s6 (`declare`) | only named assets published; no enumeration |
| US-6 — runtime register / replace | P2-s6 (`register` / `replace`) | programmatic equivalents of the sidecar |
| §4.3 transparent per-class staging | P1-s1 (probe) + P1-s4 (staging) | author never sees it; shape from the probe |
| §6 navigable `kcdx.plugin.<a>.<p>.*` namespace | P2-s5 | `__index` resolvers; general primitive |
| §7 resolution facts (FOpen / AdjustFileName / flag / root) | landed foundation + P1-s2 | resolve by name/id; no new seed row |
| §8 probe-1 — can a non-vanilla path load via a game API | P1-s1 | the gating unknown; build the resolution to its result |
| §8 probe-2 — staging lifecycle (ephemeral vs tracked) | P1-s1 → pins P1-s4 | decided by probe-1's mechanism |
| §9.2 stale-comment sweep (`kcdx.hook` `__index` / rule prose) | P2-s5b (own step) | distinct concern; commit-grain on its own |
| C++ mirror surface (`kcdxAssetInterface`, full parity) | P2-s7 | each Lua verb's mirror, same model |
| §10 manipulation (texture transforms) | **DEFERRED (§10)** | reserved + NYI doc entry; heavy codec dep, use case not concrete — built later if requested |
| public author guide `docs/asset-replacement.md` | **DEFERRED** (downstream) | authored when the surface lands (`docs-discipline.md`); not a build step here |

Every design element resolves to a step or an explicit deferral.

## Build-gated unknown (the plan's opening probe — `results-driven.md`)

The simple-replacement map (US-1, memory-mapped) is live-verified. The
code-reference + staging half rests on ONE unverified mechanism: **can a
code-referenced / staged non-vanilla asset path load via a game API?** (the
override worked because the game requests the vanilla path; a new asset is never
auto-requested — the author's code hands the path to a game loader). **P1-s1 is
the probe** that settles it; the staging shape (P1-s4) + the resolution the
surface hands back (P2-s6) are built to its result, not guessed.
