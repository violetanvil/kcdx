# Asset replacement — the settled design (Phase 8.5)

**Status:** v1 (settled 2026-06-02). **Supersedes:** the asset-replacement
design in [`../00-original-plan.md`](../00-original-plan.md) §"kcdx replaces pak
mods" (that section is trimmed to a pointer here). **Authoritative for:** the
re-decomposed Phase 8.5 steps 3–5 (the executor builds to THIS doc, not to a
step-doc summary of it).

This is the canonical spec for how a mod author adds, replaces, and references
game assets in kcdx. It is the most-touched mod-authoring surface for total
conversions, so the design optimizes for author UX first (the disassembler
test: the author declares files + intent, never engine internals).

---

## §1 Vision

**A mod author drops asset files in one `assets/` folder, declares any
replacements next to the file they apply to, and references any asset — their
own or another mod's — by path or by a published name, with zero engine
knowledge and no implicit magic.**

v1 success criteria:
- A TOML-only plugin (no code) can replace a vanilla asset by declaring it.
- An author references their own asset by its path in code; another plugin
  references it by a navigable namespace, with no quoted-namespace ceremony.
- A mod composes on top of another mod's asset pack (replaces or references its
  assets), expressible declaratively and in code.
- Nothing is replaced implicitly — every replacement is an explicit declaration,
  so a mod's effect is readable and a typo is a loud error, never a silent no-op.

Top-level architecture decision: the published game is **paks-only**
(`sys_pakPriority 2`; loose files ignored — §7 evidence), so every overlay routes
through the engine's file-open resolver hook. There is no native loose-file path
to ride; the hook IS the mechanism.

---

## §2 Glossary

- **Asset** — a non-code game resource: a texture (`.dds`), UI flash (`.gfx`),
  model (`.cgf`/`.cdf`), table/script data (`.xml`/`.lua`), sound (`.ogg`), etc.
- **`assets/` folder** — `<plugin>/assets/`, the one folder holding a plugin's
  asset files. A file's presence makes it *referenceable*; it does **not** by
  itself replace anything.
- **Override (replacement)** — making the engine serve a plugin's asset where it
  would have served a vanilla (or another mod's) asset. Always **explicitly
  declared**, never inferred from a path coincidence.
- **Add / new asset** — a plugin asset with no vanilla counterpart, referenced by
  the author's own code or another plugin's, by path or by published name.
- **Published name** — a deliberate, stable, dot-addressable name an author gives
  a subset of their assets so other mods can reference them as a contract:
  `<author>.<plugin>.<bare>` (the shared-namespace model, `naming-namespaces.md`).
- **Asset class — memory-mapped vs handle-consumed** — how the engine consumes an
  opened file. Memory-mapped (`.dds`/textures): the engine maps the bytes;
  resolves from the plugin's `assets/` dir directly. Handle-consumed
  (`.lua`/`.xml`/scripts): read through the returned file handle; needs the file
  reachable under a resolvable engine root (§4 staging). The author never sees
  this distinction (§4).
- **Sidecar** — an opt-in per-asset metadata TOML co-located with an asset,
  declaring what it replaces and/or a published name. Same idiom as the existing
  `targets.toml` sidecar (`docs/cpp/targets.md`).
- **Navigable namespace** — `kcdx.plugin.<author>.<plugin>.*` resolved by chained
  `__index` metamethods (the established `kcdx.hook.<name>` pattern, §6), so a
  cross-plugin reference reads as native dotted Lua, not a quoted string.

---

## §3 User Stories & Acceptance Criteria

### US-1 — Replace a vanilla asset, no code (the dominant TC case)

The author drops `assets/Libs/UI/Textures/KCDLogo.dds` and a sidecar declaring it
replaces the vanilla logo. The replacement shows in-game; nothing else changes.

**Acceptance:** a TOML-only plugin whose asset sidecar declares
`replaces = "<vanilla path>"` shows the replacement in-game AND the engine log
emits the overlay-hit line (winning plugin + vpath). A plugin with the file but
NO sidecar declaration replaces nothing (existence ≠ replacement). A sidecar
naming a `replaces` target that does not exist is a LOUD error, not a silent
no-op (`anti-patterns.md` AP14 — errors that teach).

### US-2 — Reference my own asset in code

The author put `assets/icons/my_icon.dds` and, in `plugin.lua`, gets a
game-loadable path to hand to a game API (set a UI texture, etc.).

**Acceptance:** `kcdx.assets.get_by_path("icons/my_icon.dds")` (own asset, no
owner prefix — the engine knows the calling plugin) returns a path a game asset
API loads. The C++ mirror (`K.assets->GetByPath`) does the same. A path to a file
not in the plugin's `assets/` returns a teaching error (names the missing path).

### US-3 — Reference another mod's asset (compose on an asset pack)

Plugin B references plugin A's asset — by A's published name, or by A's asset
path — through the navigable namespace, reading like native Lua.

**Acceptance:** `kcdx.plugin.redmoon.outfit_swap.assets.get_by_name("shirt")`
returns A's published `shirt` asset as a loadable path; `…get_by_path("male/shirt.dds")`
returns A's asset by path. Resolution honors self > engine > other
(`naming-namespaces.md`). Referencing a non-existent plugin or asset is a teaching
error.

### US-4 — Replace another mod's asset (chain)

Plugin B declares it replaces plugin A's asset (the chain mod-B-over-mod-A). The
target is A's published name or A's owner+path; B's asset wins by load order.

**Acceptance:** a sidecar whose `replaces` is another mod's published name
(`"redmoon.outfit.belt"`) or `replaces_plugin` + `replaces_path` pair makes B's
asset serve where A's would. Two plugins replacing the same target: the
load-order winner serves; the loser gets a "lost to plugin X" conflict line
(the established conflict-report shape, §5 of step 2's `conflict_engine` model).

### US-5 — Publish a name as a shared contract

The author publishes a subset of their assets under stable names so other mods
build on them, without enumerating every file.

**Acceptance:** a sidecar `name = "shirt"` (or `kcdx.assets.declare("shirt",
"male/shirt.dds")`) publishes `<author>.<plugin>.shirt`, resolvable by US-3 from
any plugin. Only named assets are published; the thousands of un-named assets
carry no declaration (no enumeration burden — the TC-scale requirement).

### US-6 — Register a dynamic / generated asset at runtime

The author makes an asset available that was not in `assets/` at load (generated
at runtime, or chosen conditionally).

**Acceptance:** `kcdx.assets.register(vpath, file)` (+ C++ mirror) makes the asset
resolvable thereafter by US-2/US-3; `kcdx.assets.replace(target, with)` (+ mirror)
registers a replacement at runtime (the conditional-replacement case). Both are
the programmatic equivalents of the declarative sidecar.

---

## §4 The model — one folder, explicit declarations, transparent staging

### §4.1 One `assets/` folder; existence ≠ replacement

`[entrypoints].assets = "assets/"` (parsed at discovery — already built, step 2)
names the one folder. Every file under it is referenceable (US-2/US-3). A file
replaces something ONLY when an explicit declaration says so (§4.2). There is NO
implicit "this path matches a vanilla path so it auto-applies" — that obfuscation
is rejected: a mod's replacements must be readable and a mistyped path must fail
loud, not silently become an orphan.

### §4.2 Replacement is declared per-asset (sidecar), or in code

**Declarative (the no-code path):** an opt-in metadata sidecar TOML, co-located
with the asset (the `targets.toml` sidecar idiom). A sidecar's **scope is its
placement**: a sidecar beside one file scopes to that file; a sidecar higher in
the tree covers its directory and subdirectories — and **the more it abstracts,
the more each entry must specify** (a file-level sidecar needs no `file` key; a
directory-level one must name which files it speaks to). The author picks their
point on that tradeoff. A sidecar declares, per asset:
- `replaces` — what this asset replaces. ONE string for a **vanilla path**
  (`"Libs/UI/Textures/KCDLogo.dds"`) or **another mod's published name**
  (`"redmoon.outfit.belt"`); OR the `replaces_plugin` + `replaces_path` **pair**
  for an unnamed cross-mod asset by path. (Mirrors the §6 resolvers: one-string
  for name/vanilla, owner+path pair for by-path. The engine errors on an
  ambiguous/both-forms sidecar — `anti-patterns.md` AP14.)
- `name` (optional) — publish this asset as `<author>.<plugin>.<name>` (US-5).

**Programmatic (the conditional path):** `kcdx.assets.replace(target, with)` and
`kcdx.assets.declare(name, file)` (+ C++ mirrors) — the runtime equivalents, for
replacement/publishing decided in code. Full Lua↔C++ parity (`lua-api-surface.md`).

### §4.3 Transparent per-class staging — the author never sees it

The author's rule is uniform: drop the file in `assets/`, declare what it
replaces. The engine absorbs the per-class loose-root detail (§2 memory-mapped
vs handle-consumed): a memory-mapped asset resolves from the plugin's `assets/`
dir directly (verified, §7); a handle-consumed asset the engine cannot resolve
from there is **transparently staged by kcdx into a `<game>/Data/`-relative
kcdx-managed root** with the loose-search flag, so it resolves. The author NEVER
needs to know which class stages where — exposing that would fail the
disassembler test (`cornerstones.md`). **The staging mechanism + lifecycle is a
build-gated unknown (§8) — pinned by step 3's first probe, not in this doc.**

### §4.4 Conflict resolution — load order, declared

When two plugins declare a replacement of the same target, the load-order winner
serves; the loser is suppressed with a conflict-report line naming winner /
suppressed / why (load order) / the fix (a lower `priority`), matching the
established `conflict_engine` winner/suppressed shape (step 2). Nothing
auto-applies, so a conflict is always between two *explicit* declarations.

---

## §5 The author-facing surface (Lua; C++ mirrors each — full parity)

Per `lua-api-surface.md`: `kcdx.assets.*` is a domain sub-table (rule 3);
discrete operations are sub-verbs (rule 4a); required args positional (rule 4).

| Verb | Shape | Purpose |
|---|---|---|
| `kcdx.assets.get_by_path(path)` | own asset, path positional | resolve own asset → loadable path (US-2) |
| `kcdx.assets.get_by_name(name)` | own published name | resolve own published asset → loadable path |
| `kcdx.assets.replace(target, with)` | runtime replacement | register a replacement in code (US-6) |
| `kcdx.assets.declare(name, file)` | runtime publish | publish a name in code (US-5) |
| `kcdx.assets.register(vpath, file)` | runtime add | make a not-at-load asset available (US-6) |

**Cross-plugin** reference uses the navigable namespace (§6), not an owner string
arg: `kcdx.plugin.<author>.<plugin>.assets.get_by_path(path)` /
`.assets.get_by_name(name)`. The path/name stays a quoted string argument (it is
data, not an identifier); the *namespace* is bare dotted.

C++ mirrors: `kcdxAssetInterface` / `K.assets->GetByPath` / `GetByName` /
`Replace` / `Declare` / `Register`, append-only ABI (`anti-patterns.md` AP11).

Each verb ships its `docs/lua/` + `docs/cpp/` entry + glossary term + a
test-plugin row in the SAME change (`docs-discipline.md`, `test-suite.md`).

---

## §6 The navigable cross-plugin namespace — `kcdx.plugin.<author>.<plugin>.*`

Cross-plugin references resolve through a navigable namespace: `kcdx.plugin`
gains an `__index` metamethod that resolves the `<author>` segment to a resolver,
whose `__index` resolves the `<plugin>` segment to a plugin handle, on which
`.assets.*` (and future cross-plugin surfaces) resolve. Each dot is a resolution
hop against engine-side namespace data — so a cross-plugin reference reads as
native dotted Lua (`kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("…")`),
with no quoted-namespace ceremony, all under the one `kcdx` global
(`lua-api-surface.md` rule 1).

**This is a GENERAL primitive, not asset-only** — once built, it fronts
cross-plugin access for any surface (assets now; hooks/events/etc. may adopt it).
It is the established kcdx pattern, NOT a new architecture: `kcdx.hook.<name>`
already resolves a bare dotted segment via a chained `__index` smart-resolver
(verified in the binder — see §9). `kcdx.plugin` today is a plain function table
(`kcdx.plugin.is_rejected("author.plugin")` takes the name as a string arg);
this design gives it (and its returned handles) the `__index` resolvers that make
the dotted form resolve.

Own-plugin references need no namespace at all — `kcdx.assets.get_by_path(path)`
resolves against the calling plugin (the engine knows who you are;
`naming-namespaces.md` "never type your own prefix").

---

## §7 RE evidence (the design rests on these verified game-binary facts)

Every fact below is verified against WHGame.dll (`release_1_5_1164953_841`),
captured in the Phase 8.5a–3a RE (fresh Ghidra + capstone + live probes); the
provenance is the verified value + its evidence tier per `reverse-engineering.md`.

- **Paks-only at the published default.** `sys_pakPriority 2` (paks-only, loose
  ignored) is the published game's only behavior; the CVar is pre-launch-only.
  *Evidence:* Warhorse "Publishing a mod" wiki + the `sys_PakPriority` CVar strings
  in the binary (prior-dump tier). → an overlay MUST hook the resolver.
- **The resolver.** `CCryPak::FOpen` (kcdx_id 131, RVA 0x004614A0, ICryPak vtable
  slot 36) is the engine-wide open-by-path resolver; all opens route through it.
  `gEnv_pCryPak` (kcdx_id 132). *Evidence:* fresh Ghidra decompile + abi_walker
  (both seed rows exist; no new seed row — `anti-patterns.md` AP18).
- **The loose-search flag.** `nFlags & 0x10000` (alone) engages the loose /
  search-path arm of the sub-resolver `CCryPak::AdjustFileName` (slot 1, RVA
  0x6205C); `0x4`/`0x2` are FOpen-internal, not the loose gate. *Evidence:* fresh
  Ghidra decompile of slot 1.
- **The loose-search root.** `AdjustFileName` roots a loose search at
  `<game>/Data/`-relative; an arbitrary absolute `assets/`-dir path is re-rooted
  or unmatched. → handle-consumed overlays stage under `<game>/Data/` (§4.3).
- **Class divergence is caller-flags, not the resolver.** `AdjustFileName` is
  extension-agnostic; `.dds` vs `.lua` differ because their caller sites pass
  different flags + consume the result differently. *Evidence:* the slot-1
  decompile (no class branch) + the live read-flag distribution.
- **Override is confirmed for memory-mapped at the plugin's `assets/` path.**
  Live: a `.dds` override from the plugin's own `assets/` dir at unforced flags
  rendered in-game. *Evidence:* live probe (the menu logo visibly changed).
- **`CCryPak::AddMod` (search-path registration) is NOT viable for loose
  overlays** at the published default (the registered root is pak-only-searched at
  `pakPriority 2`). *Evidence:* fresh Ghidra decompile of slot 19 + the per-entry
  pakPriority precedence trace. → the per-open FOpen redirect (not search-path
  registration) is the mechanism.

(The detailed RE write-ups + the probe findings live in the private RE tree; this
doc states the verified facts the design rests on. The public author-facing guide,
`docs/asset-replacement.md`, restates only the *what*, never the RE provenance —
`public-private-boundary.md`.)

---

## §8 Build-gated unknowns (recorded — NOT design forks; the build's first probes)

The simple-replacement map (US-1, memory-mapped) is **verified live**. The
code-reference + staging half (US-2/US-3 for handle-consumed, and the resolvable
path a `get_by_path` hands to a game API) rests on one unverified mechanism:

1. **Can a code-referenced / staged non-vanilla path load via a game API?** The
   override worked because the *game* requests the vanilla path; a new asset is
   never auto-requested — the author's code hands the path to a game loader, and
   that path must resolve through the hook. UNVERIFIED. **This is step 3's first
   probe** (`results-driven.md`); the `get_by_path`/`get_by_name` resolution shape
   is built to whatever it resolves.
2. **Staging lifecycle** (ephemeral-regenerate vs tracked-invalidate) — pinned
   AFTER probe 1 settles the staging mechanism, not designed on an unverified
   resolution.

These are recorded as the build's opening probes, per `results-driven.md` — the
design does not guess them.

---

## §9 Structure + the stale-comment sweep (a build deliverable)

### §9.1 Responsibility units

- `src/asset_overlay.{h,cpp}` — the overlay map + the production `CCryPak::FOpen`
  hook (built: steps 1–2). Step 3 fills the redirect; staging lands here.
- `src/lua_bind_assets.cpp` — the `kcdx.assets.*` Lua surface (step 4).
- `src/lua_bind_plugin.cpp` — extended with the `__index` navigable-namespace
  resolvers (§6); the general primitive lands here.
- `kcdxAssetInterface` in `include/kcdx/Interfaces.h` + `src/interfaces.cpp` — the
  C++ mirror (step 4), append-only.
- `docs/asset-replacement.md` — the PUBLIC author guide (a separate downstream
  artifact, authored when the surface is built, `docs-discipline.md`; NOT this
  internal spec).

### §9.2 Stale-comment sweep — a tracked build deliverable

This design relies on the navigable-namespace primitive being the SAME chained
`__index` resolver `kcdx.hook.<name>` already uses (§6). During this design,
stale comments / rule prose obscured that `kcdx.hook.<name>` resolves dotted
segments dynamically (they read as if only registered fields resolve), which
briefly led to a wrong "you can't dereference a namespace with dots in Lua"
conclusion. **A build step sweeps and corrects those stale comments** so the next
reader is not misled: the binder comments around the `kcdx.hook` `__index`
smart-resolver and any rule prose implying cross-plugin dotted access is
impossible. (The actual `src/` + `.claude/rules/` edits land via the build/execute
flow, not this design doc — recorded here as a deliverable so `/plan` schedules
it.) Scope: the comments that misstate how dotted dynamic resolution works; the
correction states that `kcdx.hook.<name>` (and the new `kcdx.plugin.<a>.<p>.*`)
resolve via `__index` metamethods against engine-side data.

---

## §10 Out of scope (deferred, reserved)

- **Manipulation** — runtime/procedural asset transforms (recolor/tint, resize,
  channel-swap, overlay-blend). Designed-but-deferred: it needs a heavy DDS/BC
  image codec (decode+edit+re-encode+mip regen — a substantial dependency,
  `dependencies.md`) whose use case (runtime edits vs authors shipping pre-edited
  files) is not yet concrete. Legitimate sequenced deferral, NOT effort-cutting
  (`cornerstones.md`). The `kcdx.assets.*` surface reserves the sub-verb space; a
  NYI doc entry marks it (`docs-discipline.md`). **Hard constraint when built:**
  the author declares the TRANSFORM (`tint = {r,g,b}`, `resize = {w,h}`), never a
  BC-block format or mip count — the disassembler test on the manipulation surface
  (`anti-patterns.md` AP12).

---

## §11 Decision record (what was settled, and what lost)

| Concern | Pick |
|---|---|
| Auto-apply by path-match | **Rejected** — implicit, obfuscated, a typo silently no-ops (AP14). Replacement is explicitly declared. |
| Replacement declaration home | A per-asset sidecar (scope = placement) AND a code verb; mirrors override having declarative + programmatic forms. Rejected: a central plugin-wide replacement map (too much abstract typing). |
| Cross-plugin reference shape | Navigable `kcdx.plugin.<author>.<plugin>.*` via `__index` (the `kcdx.hook` pattern). Rejected: an owner-string arg (`get_by_path("author.plugin", "path")` — clunky); rejected: a packed single string (`"author.plugin::path"` — a fragile new delimiter). |
| Search-path registration (`AddMod`) as the overlay mechanism | **Rejected** — pak-only at the published default (§7); does not serve loose overlays. The per-open FOpen redirect is the mechanism. |
| Manipulation this phase | **Deferred** (reserved + NYI) — heavy codec dependency, use case not concrete. |
| Asset-class staging visible to the author | **Rejected** — the engine stages transparently; the author's rule is class-agnostic (disassembler test). |

---

## Pointer for the original plan

[`../00-original-plan.md`](../00-original-plan.md) §"kcdx replaces pak mods" is
trimmed to point here; this doc is the authoritative asset-replacement design.
Steps 1–2 (the FOpen production hook + the `[entrypoints].assets` parse +
load-order overlay map) are built and live (`9e524ae`, `2588b33`); steps 3–5 are
re-decomposed against THIS doc by `/plan`.
