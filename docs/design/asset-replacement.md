# Asset replacement — the settled design

**Status:** v2 (mechanism settled 2026-06-03; changelog `asset-replacement-changelog.md`).
**Supersedes:** the prior Phase 8.5 asset-design at
`docs/outstanding-work/restructure/phase-08.5-asset-replacement/asset-design.md`
(now trimmed to a pointer here) AND the asset-replacement section in
`docs/outstanding-work/restructure/00-original-plan.md` §"kcdx replaces pak
mods". **Authoritative for:** the asset-system build — the executor builds to
THIS doc, not to a step-doc summary of it (`.claude/rules/spec-conformance.md`).

This is the canonical spec for how a mod author adds, replaces, and references
game assets in kcdx — the most-touched mod-authoring surface for total
conversions. The design optimizes for author UX first (the disassembler test:
the author declares files + intent, never engine internals — `cornerstones.md`).

**Key changes in v2 (the seam, settled on the gated load-path research — see the
changelog + §7):**
- **The seam is TWO coordinated hooks, not one.** kcdx owns (1) the resolution
  DECISION by replacing `CCryPak::AdjustFileName` (slot 1, id 152) — which file
  wins, for every asset class and both byte-lanes — AND (2) the loose-overlay
  OPEN by returning its own CRT `FILE*` (the Around-`FOpen` mechanism, gate-verified
  end-to-end). kcdx owns both the which-file decision and the handle, and depends
  on the vanilla engine's loose-search for NEITHER (§7).
- **Why two hooks (the gated finding that corrects v1):** the load-path map
  (commit `3193e84`) established that every class opens via `FOpen` but bytes
  arrive on TWO lanes — a loose lane (`FOpen` mints a `FILE*`) and a mount/stream
  lane (pak-resident assets read on a mount-minted handle). The resolver (slot 1)
  IS `sys_pakPriority`-gated — at the published default it tests pak-only, so a
  loose overlay is never resolved for a vanilla path by the engine's own search.
  Owning the resolver makes kcdx decide which file wins above that gate; owning
  the open (return our `FILE*`) makes kcdx serve the loose file without ever
  depending on the engine's loose-search succeeding. The v1 claims "a single
  replaced function is the whole seam" and "independent of `sys_pakPriority`
  because we sit above the table" were the FOpen-era framing — corrected here.
- **`sys_pakPriority` is still NOT a mechanism kcdx sets or rides** — kcdx neither
  sets the CVar nor depends on its value, because kcdx owns both the decision
  (above the gate) and the open (its own handle). The durability point holds: no
  dependence on a dev-mode CVar a game update could change.
- **Seam install timing** (§8) — both hooks install inside the already-shipping
  ready-bracket; the `ModManager_ctor`-vs-first-read ordering is a build-step-1
  probe.

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
- A stock Nexus/Workshop pak mod loads unchanged, today (§7 backward-compat).

**Top-level architecture decision: kcdx OWNS both the asset-resolution DECISION
and the loose-overlay OPEN.** The same way kcdx replaced the native mod loader
(kcdx is now the mod loader), kcdx takes over the engine's asset-resolution
decision and the loose-file open, and runs them to its own spec. Two coordinated
hooks (§7): (1) replace `CCryPak::AdjustFileName` (slot 1) so kcdx decides WHICH
file wins — above the engine's `sys_pakPriority` mode gate, for every asset class
and both byte-lanes; (2) on a loose-overlay hit, return kcdx's OWN CRT `FILE*` so
the engine reads kcdx's file without kcdx depending on the engine's loose-search
ever finding it. kcdx negotiates with neither the `sys_pakPriority` modes nor the
loose-file search behavior — it owns the decision above them and the handle
beneath them. Everything else (the pak mount machinery, the read family, the
existence leaves) is the engine's and is reused by calling through on a miss. This
is what makes startup-asset mods possible (kcdx resolves before the game's first
asset read, §8), what makes "replace ANY asset" reach the pak/mount lane (the
resolver decision is the one point every lane consults — §7), and what makes the
system durable against a game update changing a mode CVar or a loose-search rule.

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
  opened file. Memory-mapped (`.dds`/textures): the engine maps the bytes.
  Handle-consumed (`.lua`/`.xml`/scripts): read through the returned file handle
  via the read family. **Both classes route through the one resolution chokepoint
  (§7), so owning that chokepoint owns both classes** — the author never sees this
  distinction (§4.3).
- **The resolution seam** — `CCryPak::AdjustFileName`, the single engine method
  every by-name file operation calls to turn a virtual asset path into a concrete
  path BEFORE any disk/pak touch. kcdx REPLACES this method's decision; this is
  the whole ownership surface (§7).
- **Sidecar** — an opt-in per-asset metadata TOML co-located with an asset,
  declaring what it replaces and/or a published name. Same idiom as the existing
  `targets.toml` sidecar (`docs/cpp/targets.md`).
- **Navigable namespace** — `kcdx.plugin.<author>.<plugin>.*` resolved by chained
  `__index` metamethods (the established `kcdx.hook.<name>` pattern, §6), so a
  cross-plugin reference reads as native dotted Lua, not a quoted string.
- **The ready-bracket** — the already-shipping kcdx init mechanism that makes the
  game-init thread WAIT (at the `ModManager_ctor` hook, on `g_kcdxReadyEvent`)
  until kcdx signals it is ready. kcdx installs the resolution seam inside this
  window so resolution is owned before the engine's first asset read (§8).

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
returns A's published `shirt` asset as a loadable path;
`…assets.get_by_path("male/shirt.dds")` returns A's asset by path. Resolution
honors self > engine > other (`naming-namespaces.md`). Referencing a non-existent
plugin or asset is a teaching error.

### US-4 — Replace another mod's asset (chain)

Plugin B declares it replaces plugin A's asset (the chain mod-B-over-mod-A). The
target is A's published name or A's owner+path; B's asset wins by load order.

**Acceptance:** a sidecar whose `replaces` is another mod's published name
(`"redmoon.outfit.belt"`) or a `replaces_plugin` + `replaces_path` pair makes B's
asset serve where A's would. Two plugins replacing the same target: the
load-order winner serves; the loser gets a "lost to plugin X" conflict line
(the established conflict-report shape, §4.4).

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

### US-7 — A stock Nexus/Workshop pak mod loads unchanged (backward-compat)

A normal pak mod distributed today loads with zero author changes, alongside
kcdx-native asset plugins.

**Acceptance:** a stock `Data/<mod>.pak` (standard PKZIP, lenient `<kcd_mod>`
manifest, zip-entry-name = vpath) mounts and its assets resolve exactly as today —
because kcdx's replaced resolver falls through to the engine's pak-membership leaf
on an overlay miss (§7). A pak-mod vpath that collides with a vanilla asset
overrides it, as it does natively. (The pak-mod load-order registration is already
kcdx-managed — `project_kcdx_init_cycle_ownership`.)

---

## §4 The model — one folder, explicit declarations, transparent staging

### §4.1 One `assets/` folder; existence ≠ replacement

`[entrypoints].assets = "assets/"` (parsed at discovery — already built) names the
one folder. Every file under it is referenceable (US-2/US-3). A file replaces
something ONLY when an explicit declaration says so (§4.2). There is NO implicit
"this path matches a vanilla path so it auto-applies" — that obfuscation is
rejected: a mod's replacements must be readable and a mistyped path must fail
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

### §4.3 Class-agnostic serving — the author never sees the asset class

The author's rule is uniform: drop the file in `assets/`, declare what it
replaces. The two-hook seam (§7) serves every class the same way, from the
plugin's own `assets/` dir: HOOK 1 (the resolver) decides kcdx's overlay wins;
HOOK 2 (the own-`FILE*` open) opens the loose file from `assets/` and hands the
engine the handle — the read family then serves kcdx's bytes. The author never
needs to know which class is which; exposing that would fail the disassembler
test (`cornerstones.md`).

**No `<game>/Data/` staging is needed** — the v1 "stage the handle-consumed class
under a kcdx-managed engine-searched root" question is RETIRED (§9): because HOOK 2
returns kcdx's OWN handle, kcdx never relies on the engine's loose-search finding
the file at a particular root, so there is no staging tree to manage and no
staging lifecycle (ephemeral-vs-tracked) to design. The file is served straight
from `assets/`. (The remaining item is the §9 runtime confirmation that the
own-`FILE*` open serves end-to-end per lane — an acceptance, not a staging design.)

### §4.4 Conflict resolution — load order, declared

When two plugins declare a replacement of the same target, the load-order winner
serves; the loser is suppressed with a conflict-report line naming winner /
suppressed / why (load order) / the fix (a lower `priority`), matching the
established `conflict_engine` winner/suppressed shape. Nothing auto-applies, so a
conflict is always between two *explicit* declarations.

---

## §5 The author-facing surface (Lua; C++ mirrors each — full parity)

Per `lua-api-surface.md`: `kcdx.assets.*` is a domain sub-table (rule 3);
discrete operations are sub-verbs (rule 4a); required args positional (rule 4).

| Verb | Shape | Purpose |
|---|---|---|
| `kcdx.assets.get_by_path(path)` | own asset, path positional | resolve own asset → loadable path (US-2) |
| `kcdx.assets.get_by_name(name)` | own published name | resolve own published asset → loadable path |
| `kcdx.assets.replace(target, with)` | runtime replacement | register a replacement in code (US-6); the string-key cross-plugin form (`replace("author.plugin.asset", with)`) is the programmatic equivalent of `kcdx.plugin.author.plugin.*` (§6) |
| `kcdx.assets.declare(name, file)` | runtime publish | publish a name in code (US-5) |
| `kcdx.assets.register(vpath, file)` | runtime add | make a not-at-load asset available (US-6) |

**Cross-plugin** reference uses the navigable namespace (§6), not an owner-string
arg: `kcdx.plugin.<author>.<plugin>.assets.get_by_path(path)` /
`.assets.get_by_name(name)`. The path/name stays a quoted string argument (it is
data, not an identifier); the *namespace* is bare dotted. The string-key form
`kcdx.assets.replace("author.plugin.asset", with)` is the equivalent for a
dynamic or quoted key the dotted form can't express (§6).

C++ mirrors: `kcdxAssetInterface` / `K.assets->GetByPath` / `GetByName` /
`Replace` / `Declare` / `Register`, append-only ABI (`anti-patterns.md` AP11).

Each verb ships its `docs/lua/` + `docs/cpp/` entry + glossary term + a
test-plugin row in the SAME change (`docs-discipline.md`, `test-suite.md`).

### §5.1 The runtime-store mechanism (settled 2026-06-04)

Two of the five verbs READ engine state that already exists; four MUTATE or READ
a runtime store. The mechanism behind the runtime four was unspecified in v2 and
is settled here (consult 2026-06-04; the build splits accordingly — §5.2).

**`get_by_path(path)` is a pure read** — it resolves the calling plugin's
identity + its `assets/` root → a loadable disk path the HOOK-2 open already
serves. It mutates nothing and depends on no runtime store; it is buildable
against existing engine state.

**The other four (`get_by_name` / `declare` / `register` / `replace`) need a
runtime store** that does not exist yet. The settled mechanism:

- **A SEPARATE runtime store, NOT a mutation of the build-time `g_overlayMap`.**
  The discovery-built overlay map (`src/asset_overlay.cpp`) stays
  *not-mutated-after-build* and lock-free — the two resolver hooks keep reading
  it with zero synchronization. Runtime additions live in a separate store the
  resolver consults ALONGSIDE the build-time map. (Preserves the hot resolver's
  lock-free read; isolates all mutation concurrency in the rarely-written store —
  `concurrency.md`, `memory.md`.)
- **Lock-free reads via an atomic-pointer (RCU) snapshot.** The store is an
  immutable snapshot behind an `atomic<const RuntimeStore*>`. A writer
  (`register`/`declare`/`replace`, a one-off author call) builds a new snapshot
  and swaps the pointer (release); the hot resolver reads load-acquire and reads
  a never-mutated snapshot — wait-free, allocation-free on the read path
  (`concurrency.md` atomics-first/locks-last; `memory.md` hot-path). Writes are
  rare, so the copy-on-write cost is irrelevant; old-snapshot reclamation is an
  implementation detail (a generation/epoch, or retain-for-session given the few
  author writes).
- **Take-effect = "thereafter" (design-determined, §3 US-6).** A runtime
  `register`/`replace` affects assets opened AFTER the call; no re-resolve of
  already-open handles. Consistent with §8 (the seam is live before the first
  read; a later mutation joins the resolution path going forward).
- **A new responsibility unit owns both runtime stores:
  `src/asset_namespace.{h,cpp}`.** It holds the runtime-overlay store (for
  `register`/`replace`) AND the published-name→path store (for
  `declare`/`get_by_name`/the §6 `get_by_name` cross-plugin form). Distinct
  responsibility from `asset_overlay.cpp` (the build-time map + the hooks), so it
  is its own unit (`structure-by-responsibility.md`, `no-monolith.md`), not bolted
  onto the hot-path resolver file. The resolver consults this unit alongside the
  build-time map.

### §5.2 Build split — `get_by_path` ships first; the runtime four follow

`get_by_path` (the pure read) is built + shipped on its own (it depends on none
of §5.1's store); the four store-dependent verbs are built in a later step AGAINST
the §5.1 mechanism. The four carry an NYI doc entry + a deliberately-failing
matrix row pinning their contract until built (`docs-discipline.md`,
`test-suite.md`). This is dependency-ordering (`incremental-delivery.md`), not a
capability cut — the end state is all five verbs at full Lua↔C++ parity.

---

## §6 The navigable cross-plugin namespace — `kcdx.plugin.<author>.<plugin>.*`

Cross-plugin references resolve through a navigable namespace. **This is the SAME
chained-`__index` mechanism `kcdx.hook.<name>` already uses** (verified in the
binder — `src/lua_bind_hook.cpp` `__index` smart-resolver), NOT a new
architecture:

- `kcdx.plugin` gains an `__index` metamethod that resolves the `<author>` segment
  to an author resolver.
- that resolver's `__index` resolves the `<plugin>` segment to a plugin handle.
- on the plugin handle, `.assets.*` (and any future cross-plugin surface) resolves
  to the operation, against engine-side namespace data.

Each dot is a resolution hop — so a cross-plugin reference reads as native dotted
Lua (`kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("male/shirt.dds")`), with
no quoted-namespace ceremony, all under the one `kcdx` global (`lua-api-surface.md`
rule 1). Resolution honors self > engine > other (`naming-namespaces.md`).

**Current state being changed:** `kcdx.plugin` is a plain function table today
(`src/lua_bind_plugin.cpp` — `kcdx.plugin.is_rejected("author.plugin")` takes the
name as a string arg, no `__index`). This design gives `kcdx.plugin` and its
returned handles the `__index` resolvers that make the dotted form resolve. The
existing `is_rejected` function-call surface stays (it is a query, not a
navigation); the `__index` resolver is additive.

**The string-key escape for dynamic / quoted keys.** Dotted navigation needs each
segment to be a valid Lua identifier. An asset key that is dynamic (computed at
runtime) or not identifier-shaped (`"shirt.dds"`, a key with a dot or dash) cannot
be a bare dotted segment. For those, the string-key form is the equivalent:
`kcdx.assets.replace("redmoon.outfit_swap.shirt", "/assets/newshirt.dds")` and
`kcdx.assets` lookups that take a packed `"author.plugin.asset"` string. The
dotted namespace is the common, glanceable path; the string-key form is the
general fallback that loses no capability.

**This is a GENERAL primitive, not asset-only** — once built, the
`kcdx.plugin.<author>.<plugin>` resolver fronts cross-plugin access for any
surface (assets now; hooks/events/etc. may adopt it). Built in
`src/lua_bind_plugin.cpp`.

Own-plugin references need no namespace at all — `kcdx.assets.get_by_path(path)`
resolves against the calling plugin (the engine knows who you are;
`naming-namespaces.md` "never type your own prefix").

---

## §7 RE evidence — the verified seam (TWO hooks; the design rests on these gated facts)

Every fact below is verified against WHGame.dll (`release_1_5_1164953_841`) and
GATED by an independent body-read verifier before it shipped as authority (the
asset load-path map, commit `3193e84`; the FOpen-handle finding, commit `e6e8e27`;
both per `research-disassembly/SKILL.md` §4.5 / `anti-patterns.md` AP19). The seed
entities are already authored (no new seed row — AP18). NOTE: the id-152 seed
PROSE still carries the v1 "FOpen calls slot 1 / single chokepoint / independent
of sys_pakPriority" claim — that prose is FALSIFIED by the load-path map and is a
build-deliverable correction (§10.2), not the authority here.

**The two structural facts that drive the seam (gated, read in the bodies):**

1. **Every asset class OPENS via `CCryPak::FOpen` (slot 36), but BYTES arrive on
   TWO lanes.** The load-path map read each class's open-site body — texture
   (`FUN_1807b5ed4`→CCryFile::Open), model (`CReadOnlyChunkFile::Read`
   `FUN_18051cba0`), audio (FMOD `useropen` `FUN_181224d1c`), script/XML
   (CCryFile::Open `FUN_1804605bc`) — all open through `FOpen`. No class
   memory-maps (zero `CreateFileMapping`/`MapViewOfFile` imports). But:
   - **LOOSE lane** — when resolution picks a loose file, `FOpen` mints a CRT
     `FILE*` (`FUN_1809b2b28`→`_wfopen`); the read family (slot 38/40) serves it
     via its OS arm. FRead's dispatch is INDEX-vs-COUNT: `handle−1` vs the
     pak-handle vector element count (`FUN_180427e40` = the `std::vector` size
     idiom); a real `FILE*` is a heap pointer so `handle−1 ≫ count` → OS arm
     (gate-verified, `e6e8e27`). **This is the lane kcdx's own-`FILE*` open serves.**
   - **MOUNT/STREAM lane** — when resolution picks a PAK-resident asset, bytes are
     read by `ZipDir::ReadFileStreaming` (`FUN_180464b88`) on a `CreateFileA`
     handle minted at pak-MOUNT (slot 72 `FUN_1804d5580`), NOT via `FOpen`.

2. **The resolution DECISION is `sys_pakPriority`-gated, and it is the one point
   BOTH lanes consult.** `CCryPak::AdjustFileName` (kcdx_id **152**, RVA `0x6205C`,
   slot 1) decides which concrete file a vpath resolves to, for every by-name
   consumer. At the published default (`sys_pakPriority 2`) its search tests
   **pak-only, no loose fallback** (gate-verified in the resolver body
   `FUN_18046205c`) — so the engine's OWN search never resolves a vanilla path to
   a loose overlay. That is WHY an FOpen-only override cannot replace a vanilla
   (pak-resident) asset, and why kcdx must own the DECISION, not just an open.

**The seam — TWO coordinated hooks (this is the corrected mechanism):**

- **HOOK 1 — the resolution DECISION: replace `CCryPak::AdjustFileName` (slot 1,
  id 152)** via `hook_chain::AddCEngine` (Around/Replace). On a declared-overlay
  HIT, kcdx returns its overlay's identity; on a MISS, it calls through to the
  engine leaves so stock content (incl. a stock Nexus/Workshop pak, US-7) resolves
  unchanged. Because the resolver runs BEFORE every lane's read and BOTH lanes
  consult it, owning it makes kcdx decide which file wins for ALL classes and BOTH
  lanes — above the `sys_pakPriority` gate. This is what reaches replace-vanilla
  (the pak/mount lane). *Evidence:* gated resolver-body decompile (`3193e84`).
- **HOOK 2 — the loose OPEN: return kcdx's OWN CRT `FILE*`.** For a loose overlay
  (a kcdx-managed file — an add-new asset, or a vanilla replacement kcdx serves
  loose), kcdx opens the file itself (`_wfopen`/`fopen`) and the seam returns that
  `FILE*`, which the read family serves via its OS arm (fact 1, gate-verified
  `e6e8e27`). This means kcdx does NOT depend on the engine's loose-search finding
  the overlay (the layer the prior path-redirect FAILED at) — kcdx owns the handle
  directly, so the loose lane works regardless of file location or `sys_pakPriority`.
  *Decision (settled 2026-06-03):* return-our-own-`FILE*` over a path-only redirect,
  so kcdx is never barred by a vanilla-engine loose-search unknown.
- **Call-through leaves (REUSE on a MISS — do not reimplement):** pak-membership
  `CCryPak::IsFileInPak` (id **153**, `0x631F0`), disk-existence
  `CCryPak::DoesFileExistOnDisk` (id **154**, `0x9C9CB4`), root-prefix normalizer
  `CCryPak::AdjustFileName_RootPrefix` (id **155**, `0x621BC`). On an overlay miss
  the resolver falls through to these so stock resolution is byte-identical to
  today. *Evidence:* fresh Ghidra decompiles (gated).
- **Reused engine machinery (touch nothing):** the read family (FRead slot 40
  `0x51CD00` + slot 38 `0x461304` + FSeek/FTell/FEof/FClose — handle-tag dispatch,
  zero resolution logic); the pak mount/archive machinery (OpenPack slots 6/7/9,
  the ZipDir parser, rank-insert — kcdx already drives the native mount via
  `pak_mod_registry`). kcdx adds the two hooks above; everything else is the
  engine's, reused by calling through.
- **Override is confirmed live for the memory-mapped class** (a `.dds` overlay
  from a plugin's `assets/` dir rendered in-game). The remaining end-to-end runtime
  confirmation (a handle-consumed `.lua` served via the own-`FILE*` open, and the
  resolver-redirect reaching the pak/mount lane for a vanilla replace) is the §9
  build probe.
- **DirectStorage caveat (flagged-unverified, default OFF).** A texture served via
  the optional DirectStorage path (`dstorage.dll`, `wh_sys_streaming_directstorage_enabled`
  default 0) may open its own handle outside `FOpen`. Default-off, so not on the
  common path; resolve before claiming "the seam serves DirectStorage textures."

(The detailed RE write-ups + gated findings live in the private RE tree
[`_research/asset-loadpath-map-recon/`, `_research/asset-fopen-handle-recon/`];
this doc states the verified facts the design rests on. The public author-facing
guide restates only the *what*, never the RE provenance — `public-private-boundary.md`.)

---

## §8 Seam install timing — the seam is live before the first asset read

For a startup-asset mod (and for any boot/menu asset), the resolution seam must be
installed and armed BEFORE the engine's first asset read. kcdx resolves everything
it needs before the game boots — the seam is one more thing kcdx readies in that
window.

**The mechanism — reuse the already-shipping ready-bracket.** kcdx already makes
the game-init thread WAIT until kcdx is ready: it installs a hook on
`ModManager_ctor`, and `HookedCtor` blocks on `g_kcdxReadyEvent` (created at init,
signaled when kcdx's mod list is built) before the game proceeds. This is built
and live in production today — it is how the mod-loader takeover works
(`src/dllmain.cpp` CreateReadyEvent → InstallCtorBracket → BuildEnabledListOnWorker
→ SetEvent; `src/mod_absorb/ctor_bracket.cpp` HookedCtor's
`WaitForSingleObject(g_kcdxReadyEvent, INFINITE)`).

**Both seam hooks install inside this same window** — kcdx installs the
`AdjustFileName` replacement (HOOK 1) and the loose-open own-`FILE*` hook (HOOK 2),
and resolves the refdb facts they need (ids 152–155), **before it signals
`g_kcdxReadyEvent`**. So the game-init thread blocks at `ModManager_ctor` until
both hooks are live; resolution + the loose-open are owned before the game proceeds
past the ctor. No new wait-mechanism is built — the hooks slot into the one that
already ships.

**The one ordering fact the build must confirm — a step-1 probe.** The bracket
guarantees the game waits *at* `ModManager_ctor`. What is NOT yet verified is
whether `ModManager_ctor` fires BEFORE the engine's first overridable asset read.
The prior FOpen-probe finding (`hits=0`) was an INSTALL-TIMING artifact — that
probe installed in the worker-thread post-RefdbOpened path (~17:43:03.4), AFTER
the first asset reads (~17:43:02.0); installing in the ready-bracket window is the
fix, but the ctor-vs-first-read ordering is the remaining checkable unknown.
**Build step 1 is a probe** (`.claude/rules/results-driven.md`): log when
`HookedCtor` fires vs the engine's first overridable asset read.
- `ctor` fires at-or-before the first read → install the seam before
  `SetEvent(g_kcdxReadyEvent)`; the bracket's wait makes the seam live before the
  game proceeds = resolution owned before the first read. The design's stated
  mechanism holds.
- the first read is EARLIER than `ctor` → a surfaced finding: the seam-install /
  ready point must move earlier than `ModManager_ctor` (a real design fork at that
  point, surfaced to the user — `design-authority.md`). This is the falsifying
  outcome; the design names it rather than assuming the convenient one.

The refdb resolution of ids 152–155 the seam needs happens at `RefdbOpened`, which
is already before the ready-bracket signal in the worker's init sequence — so the
name-resolved seam can install in the ready window. (If the step-1 probe shows the
first read precedes even `RefdbOpened`, the earlier-resolution question is part of
the same surfaced finding.)

---

## §9 Build-gated unknowns (recorded — NOT design forks; the build's first probes)

The mechanism is now gated-verified statically (the two-hook seam, §7); what
remains are RUNTIME confirmations the build resolves by probe FIRST, per
`.claude/rules/results-driven.md` — the design does not guess them. The earlier
v1 "does a loose handle-consumed overlay resolve / does it need staging?" unknown
is RETIRED: HOOK 2 (return kcdx's own `FILE*`) removes the dependence on the
engine's loose-search, so there is no path-resolution-into-the-engine to probe and
no `<game>/Data/` staging to design — kcdx owns the handle directly. The remaining
build-gated runtime confirmations:

1. **Seam install ordering** (§8) — does `ModManager_ctor` fire before the first
   asset read? **Build step 1's probe** (the existing ctor-vs-first-read probe).
   Settles WHERE the two hooks install; a falsifying outcome is a surfaced fork.
2. **The two hooks serve end-to-end, live, per lane** — one probe confirms: (a) a
   handle-consumed `.lua` overlay served via HOOK 2's own-`FILE*` open reads
   kcdx's bytes in-game (the gate-verified static mechanism, confirmed live); and
   (b) HOOK 1's resolver-redirect makes the pak/mount lane serve kcdx's file for a
   vanilla (pak-resident) REPLACE. The static seam is gated; this is the runtime
   acceptance, not a mechanism unknown.
3. **The cFn-ABI pointer-return** — HOOK 2 returns a `FILE*` (pointer-width) into
   the resolver/FOpen `rv` slot through the kcdx hook chain; a build item to
   confirm the chain threads a pointer return cleanly (hook-chain mechanics, not a
   binary fact).
4. **DirectStorage texture arm** (default OFF, §7 caveat) — confirm it does not
   bypass the seam for textures, OR scope it out for v1 as a default-off path.

These are the build's opening probes/confirmations (ordered before the surfaces
that depend on them — `.claude/rules/incremental-delivery.md`), not design forks.

---

## §10 Structure + the stale-comment sweep (a build deliverable)

### §10.1 Responsibility units

- `src/asset_overlay.{h,cpp}` — the overlay map + the resolution seam. **The
  production seam REPLACES `CCryPak::AdjustFileName`** via
  `hook_chain::AddCEngine`, resolved by name (`CCryPak_AdjustFileName`, id 152);
  the overlay-map lookup + the call-through to the leaves (153/154/155) live here.
  (Steps 1–2 built the overlay map + an earlier FOpen probe site; the FOpen probe
  is removed and replaced by the production `AdjustFileName` seam — the FOpen hook
  is not the mechanism. The throwaway `InstallSeamAProbe()` diagnostic in this
  module is removed when the seam is captured, leaving no residue —
  `.claude/rules/working-artifacts.md`.)
- `src/lua_bind_assets.cpp` — the `kcdx.assets.*` Lua surface (§5).
- `src/lua_bind_plugin.cpp` — extended with the `__index` navigable-namespace
  resolvers (§6); the general `kcdx.plugin.<author>.<plugin>` primitive lands here,
  additive to the existing `is_rejected` function table.
- `kcdxAssetInterface` in `include/kcdx/Interfaces.h` + `src/interfaces.cpp` — the
  C++ mirror (§5), append-only.
- The PUBLIC author guide (the downstream `docs/lua/` + `docs/cpp/` reference
  entries + glossary terms, authored when each surface is built —
  `docs-discipline.md`; NOT this internal spec; restates only the *what*, never
  the RE provenance — `public-private-boundary.md`).

### §10.2 Stale-comment sweep — a tracked build deliverable

During this design, stale prose (in the prior research/design notes, and any rule
prose implying cross-plugin dotted access is impossible) obscured that
`kcdx.hook.<name>` resolves dotted segments dynamically via a chained `__index`
smart-resolver — which briefly led to a wrong "you can't dereference a namespace
with dots in Lua" conclusion. The production `src/` binder comments around the
`kcdx.hook` `__index` resolver are CORRECT and document the mechanism well
(`src/lua_bind_hook.cpp` ~1106) — the misleading content was in the
research/design layer, not the live source.

**A build step sweeps and corrects the stale prose** so the next reader is not
misled: any research/design note or rule prose implying dotted dynamic resolution
(`kcdx.hook.<name>`, and the new `kcdx.plugin.<a>.<p>.*`) does not work, or is
impossible, is corrected to state it resolves via `__index` metamethods against
engine-side data. Scope: the stale prose that misstates how dotted dynamic
resolution works — NOT the (correct) production binder comments. (The actual edits
land via the build/execute flow, recorded here as a deliverable so `/plan`
schedules it.)

**Second sweep target — the falsified id-152 seed prose.** The
`data/seeds/address_names_seed.csv` row for `CCryPak_AdjustFileName` (id 152) still
asserts the v1 claim: "Every by-name file consumer (FOpen, …) calls `*(*pCryPak+8)`
… the single chokepoint kcdx replaces … independent of sys_pakPriority." The gated
load-path map (`3193e84`) FALSIFIED this — FOpen does NOT call slot 1, and the
resolver IS `sys_pakPriority`-gated (pak-only at the default). A build step
corrects the id-152 prose to the verified two-lane / two-hook model (slot 1 is the
resolution DECISION every by-name consumer's eventual resolution flows through, but
FOpen mints the handle independently; the seam is the resolver + the own-`FILE*`
open, not slot-1-alone). Editing the seed CSVs is the working/`/execute` flow
(not `/design`'s scope), recorded here so `/plan` schedules it. This is itself an
AP19/AP2 correction — a falsified inference left in an authoritative record.

---

## §11 Out of scope (deferred, reserved)

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

## §12 Decision record (what was settled, and what lost)

| Concern | Pick |
|---|---|
| **The ownership mechanism (v2 — corrected)** | **TWO coordinated hooks: (1) replace `CCryPak::AdjustFileName` (slot 1, id 152) for the resolution DECISION + (2) return kcdx's own CRT `FILE*` for the loose OPEN.** Owning the decision reaches all classes + both byte-lanes (incl. pak-resident replace, above the `sys_pakPriority` gate); owning the open serves the loose file without depending on the engine's loose-search. Cornerstone-settled (UX #1 + Capability #2 win; Perf #3 — per-FS-query, off the per-read hot path — never traded up). **Rejected:** slot-1-alone (the v1 claim "single chokepoint, FOpen calls slot 1" — FALSIFIED by the load-path map; FOpen mints the handle independently, and slot-1-alone leaves the loose-open at the mercy of the engine's pak-only-at-default search). **Rejected:** hooking only `CCryPak::FOpen` (covers loose add-new but NOT pak-resident replace — the resolver decides that). **Rejected:** kcdx-mounts-a-pak (rides the proven pak path but fights the loose-file author UX with a pack/repack lifecycle — loses UX #1). |
| **The loose OPEN — own-`FILE*` vs path-redirect** | **Return kcdx's own CRT `FILE*` (HOOK 2)** — gate-verified end-to-end (`e6e8e27`): FRead's OS arm serves any `FILE*` (`handle−1 ≫ pak-count`). **Rejected:** a path-only redirect (return the overlay path, let the engine's `FOpen` open it) — depends on the engine's loose-search finding the file at the default, the exact layer the prior redirect FAILED at; user-settled to not be barred by a vanilla-engine loose-search unknown. |
| **`sys_pakPriority` as a mechanism** | **Rejected — neither set nor depended on.** kcdx owns the DECISION (above the mode gate) and the OPEN (its own handle), so resolution does not ride the CVar. The resolver IS gated (pak-only at the default — the load-path map; the v1 "independent because we sit above the table" was true of the decision but missed that an FOpen-only override still couldn't replace a pak asset). A dev-mode CVar a game update could change stays out of the mechanism. (Non-negotiable.) |
| **Seam install timing** | Install BOTH hooks inside the already-shipping ready-bracket (before `SetEvent(g_kcdxReadyEvent)`), so both are live before the first asset read; a build-step-1 probe confirms the `ModManager_ctor`-vs-first-read ordering, a falsifying outcome is surfaced. **Rejected:** wiring in without confirming the ordering (the unverified-timing assumption that caused the `hits=0` result); rejected: a new wait-mechanism (the ready-bracket already ships). |
| **Auto-apply by path-match** | **Rejected** — implicit, obfuscated, a typo silently no-ops (AP14). Replacement is explicitly declared. |
| **Replacement declaration home** | A per-asset sidecar (scope = placement) AND a code verb; mirrors override having declarative + programmatic forms. **Rejected:** a central plugin-wide replacement map (too much abstract typing). |
| **Cross-plugin reference shape** | Navigable `kcdx.plugin.<author>.<plugin>.*` via chained `__index` (the `kcdx.hook` pattern), with `kcdx.assets.replace("author.plugin.asset", with)` as the string-key equivalent for dynamic/quoted keys. **Rejected:** an owner-string arg (`get_by_path("author.plugin", "path")` — clunky); rejected: a packed single delimiter (`"author.plugin::path"` — a fragile new delimiter); rejected: a function-call-only accessor with no dotted navigation (breaks surfaces-mirror, contradicts the verbatim ask). |
| **Search-path registration (`AddMod`) as the overlay mechanism** | **Rejected** — pak-only-searched at the published default; does not serve loose overlays, and is orthogonal to the resolution decision the seam owns. The seam-replace is the mechanism. |
| **Manipulation this phase** | **Deferred** (reserved + NYI) — heavy codec dependency, use case not concrete. |
| **Asset-class staging visible to the author** | **Rejected** — the engine stages transparently (if staging is needed at all — §9 probe 2); the author's rule is class-agnostic (disassembler test). |
| **Backward-compat with stock Nexus/Workshop paks** | **Required + free** — the seam's overlay-miss fall-through to the pak-membership leaf (id 153) resolves stock pak content unchanged; a stock pak is standard PKZIP the engine's own mount machinery handles (US-7, §7). |

---

## Pointer for the prior plan

The prior Phase-8.5 design at
`docs/outstanding-work/restructure/phase-08.5-asset-replacement/asset-design.md`
is trimmed to a pointer here; THIS doc is the authoritative asset-replacement
design. The overlay map + `[entrypoints].assets` parse are built and live; the
seam + surfaces are re-decomposed against THIS doc by `/plan` (the prior
asset-replacement plan tree at `docs/outstanding-work/asset-replacement/` was
built around the superseded FOpen-redirect mechanism and is re-planned against
this seam).
