# Asset system — plan spec (shared spec for every step)

The implementation plan for kcdx's **asset system** — the full surface by which a
mod author adds, references, publishes, composes, and replaces game assets. Every
step doc leans on this spec rather than restating shared context.

This is NOT "asset replacement" alone — replacement is one capability among
several. The system lets an author: **add** new assets on systems they create,
**reference** any asset (own or another mod's) in Lua/C++, **publish** stable
names as a cross-mod contract, **compose** on top of another mod's asset pack, and
**replace** vanilla or another mod's assets — all with zero engine knowledge and
no implicit magic.

- **Goal:** ship kcdx's asset system — author drops files in one `assets/` folder;
  references any asset (own or another mod's) by path or published name through a
  navigable namespace; declares replacements explicitly (sidecar or code);
  registers/publishes assets at runtime; with transparent per-class handling and
  zero engine knowledge.
- **Settled design (the authority — build to IT, not to a step's prose summary):**
  [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
  (the canonical `§`-structured TRD, committed `eea0fdb`). Every step doc cites the
  specific `§` it builds. `.claude/rules/spec-conformance.md`: a step doc is a
  pointer to that design, never a replacement.

## Landed foundation (built — this plan builds ON these, does not rebuild them)

- **`[entrypoints].assets` parse + the load-order overlay map** (vpath → loose
  file; `NormalizeVPath`; built at discovery), `src/config.cpp` +
  `src/asset_overlay.cpp` + `src/plugin_loader.{h,cpp}` — committed `2588b33`. The
  map (vpath → winning OverlayEntry) and its load-order conflict reporting are
  built; the resolution seam that CONSULTS the map is this plan's P1.

NOT foundation — to be removed by P1: the throwaway `InstallSeamAProbe()` SEAM-A
diagnostic + the dead `CCryPak::FOpen` probe hook in `src/asset_overlay.{h,cpp}`.
The earlier "production FOpen overlay hook" (`9e524ae`) is superseded — the FOpen
hook is NOT the mechanism (design §7); P1-s2 replaces it with the `AdjustFileName`
seam and removes the FOpen/SEAM-A residue (`.claude/rules/working-artifacts.md` —
a probe leaves no residue in live source).

## Cross-step invariants (hold across every step)

- **kcdx OWNS asset resolution by REPLACING `CCryPak::AdjustFileName`** (the
  resolution-decision root, slot 1, id 152) — NOT by hooking `CCryPak::FOpen`. On
  an overlay HIT the kcdx replacement returns kcdx's path; on a MISS it falls
  through to the engine's leaves (pak-membership 153, disk-existence 154,
  root-prefix 155) so stock content resolves unchanged (design §7). Owning the one
  seam owns BOTH asset classes (memory-mapped + handle-consumed).
- **Resolution is independent of `sys_pakPriority`.** kcdx's overlay decision sits
  ABOVE the engine's per-mode existence table; kcdx neither sets nor depends on the
  CVar. `sys_pakPriority` is NOT a mechanism, fallback, or dependency anywhere in
  this plan (design §7, §12 — a dev-mode CVar a game update could change is an
  unacceptable durability risk).
- **The seam is live before the engine's first asset read** — installed inside the
  already-shipping ready-bracket (before `SetEvent(g_kcdxReadyEvent)`), so the
  game-init thread blocks at `ModManager_ctor` until the seam is live (design §8).
  The `ModManager_ctor`-vs-first-read ordering is P1-s1's probe.
- **Explicit declaration, never implicit path-match.** A file's presence in
  `assets/` does NOT replace anything; a replacement is an explicit sidecar or code
  declaration. A mistyped target fails LOUD (`anti-patterns.md` AP14), never a
  silent orphan (design §4.1).
- **Resolve game facts by name/id, never a literal** (`no-hardcoded-addresses.md`,
  AP1). `CCryPak_AdjustFileName` (id 152), `CCryPak_IsFileInPak` (153),
  `CCryPak_DoesFileExistOnDisk` (154), `CCryPak_AdjustFileName_RootPrefix` (155) —
  seed rows exist; **no new seed row this plan** (AP18).
- **Full Lua↔C++ parity** (`lua-api-surface.md`) — every author surface ships its
  Lua verb AND its `kcdxAssetInterface` C++ mirror; parity is tested.
- **Docs move with the surface** (`docs-discipline.md`) — each `kcdx.assets.*`
  verb / interface method ships its `docs/lua/` + `docs/cpp/` entry + glossary term
  + a test row in the SAME step.
- **The author never sees the asset-class distinction** (memory-mapped vs
  handle-consumed) — handling is transparent (design §4.3, the disassembler test).
- **A stock Nexus/Workshop pak loads unchanged** — the overlay-miss fall-through to
  the pak-membership leaf (id 153) resolves stock pak content exactly as today
  (design §3 US-7, §7); a regression row proves it.

## Coverage map — every design element → its covering step (or explicit deferral)

`.claude/rules/spec-conformance.md` — the durable proof the plan accounts for the
whole design. A later `/execute` / review reads this to verify every element shipped.

| Design element | Covered by | Notes |
|---|---|---|
| §8 seam-install ordering (does `ModManager_ctor` precede first read) | P1-s1 (probe) | the gating unknown; falsifying outcome surfaces a fork |
| §7 the resolution seam — REPLACE `AdjustFileName` (id 152) + call-through leaves | P1-s2 | removes the FOpen/SEAM-A residue; memory-mapped override live end-to-end |
| §4.3 / §9-probe-2 handle-consumed resolution + transparent staging | P1-s3 (probe + build) | does `.lua`/`.xml` resolve from `assets/` or need staging — built to the result |
| §4.2 declarative sidecar (`replaces` / `replaces_plugin`+`replaces_path` / `name`) | P1-s4 | feeds the overlay map; loud error on missing target (AP14) |
| §4.4 load-order conflict resolution + report line | P1-s4 | winner/suppressed shape; map's conflict reporting already built (2588b33) |
| US-1 — replace a vanilla asset, no code (declarative) | P1-s2 (resolution) + P1-s4 (sidecar) | dominant TC case; memory-mapped live-verified |
| US-4 — replace another mod's asset (chain) | P1-s4 (sidecar target) + P1-s2 (resolution) | target = name or owner+path; load-order conflict |
| US-7 — stock Nexus/Workshop pak loads unchanged | P1-s2 (miss fall-through) + P3-s9 (regression row) | backward-compat; the overlay-miss path resolves stock paks |
| §6 navigable `kcdx.plugin.<author>.<plugin>.*` namespace (`__index` chain) | P2-s5 | the general cross-plugin primitive; US-3 enabler |
| §10.2 stale-comment sweep (research/design prose on dotted `__index`) | P2-s6 (own step) | distinct concern; commit-grain on its own; cites the as-built P2-s5 |
| §5 `kcdx.assets.*` Lua surface (`get_by_path`/`get_by_name`/`replace`/`declare`/`register`) | P2-s7 | + the string-key cross-plugin form |
| US-2 — reference own asset by path | P2-s7 (`get_by_path`) | own = no owner prefix |
| US-3 — reference another mod's asset (navigable ns) | P2-s5 (namespace) + P2-s7 (surface) | `kcdx.plugin.<a>.<p>.assets.*` |
| US-5 — publish a name as a contract | P1-s4 (`name` sidecar) + P2-s7 (`declare`) | only named assets published; no enumeration |
| US-6 — runtime register / replace | P2-s7 (`register` / `replace`) | programmatic equivalents of the sidecar |
| §5 / §10.1 C++ mirror surface (`kcdxAssetInterface`, full parity) | P2-s8 | each Lua verb's mirror, append-only ABI |
| §11 manipulation (texture transforms) | **DEFERRED (§11)** | reserved + NYI doc entry; heavy codec dep, use case not concrete — built later if requested |
| §10.1 public author guide (`docs/lua/` + `docs/cpp/` entries) | ships per-step with each surface (`docs-discipline.md`) | each verb's entry lands in its building step; not a separate build step |

Every design element resolves to a step or an explicit deferral.

## Build-gated unknowns (the plan's opening probes — `results-driven.md`)

Two mechanisms rest on unverified facts the build resolves by probe FIRST; the
dependent surface is built to whatever the probe resolves, never guessed:

1. **Seam-install ordering (P1-s1).** Does `ModManager_ctor` fire BEFORE the
   engine's first overridable asset read? Settles WHERE the seam installs in
   kcdx's init sequence (design §8). A falsifying outcome (first read earlier than
   the ctor) is a surfaced design fork, not a silent workaround.
2. **Handle-consumed resolution + staging (P1-s3).** Does a declared overlay served
   from the plugin's `assets/` dir resolve end-to-end for the handle-consumed class
   (`.lua`/`.xml`) through the replaced seam, or does that class need kcdx to stage
   the file under a kcdx-managed root first? The memory-mapped class is live-verified
   (design §7); this class is the probe. Settles §4.3's staging question + the path
   `get_by_path`/`get_by_name` hand back (P2-s7). The staging lifecycle
   (ephemeral-regenerate vs tracked-invalidate) is pinned AFTER this probe, never
   designed on an unverified resolution.
