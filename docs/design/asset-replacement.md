# Asset replacement — the settled design

**Status:** v1 (settled 2026-06-02). **Supersedes:** the prior Phase 8.5
asset-design at `docs/outstanding-work/restructure/phase-08.5-asset-replacement/asset-design.md`
(now trimmed to a pointer here) AND the asset-replacement section in
`docs/outstanding-work/restructure/00-original-plan.md` §"kcdx replaces pak
mods". **Authoritative for:** the asset-replacement build — the executor builds
to THIS doc, not to a step-doc summary of it (`.claude/rules/spec-conformance.md`).

This is the canonical spec for how a mod author adds, replaces, and references
game assets in kcdx — the most-touched mod-authoring surface for total
conversions. The design optimizes for author UX first (the disassembler test:
the author declares files + intent, never engine internals — `cornerstones.md`).

**Key changes from the prior (Phase-8.5) design:**
- **Mechanism corrected to the verified seam.** kcdx **owns asset resolution by
  REPLACING `CCryPak::AdjustFileName` (the resolution-decision root), not by
  hooking `CCryPak::FOpen`.** The prior doc named FOpen (slot 36) as the hook and
  framed the design as riding the engine's `sys_pakPriority` mode; the 5-front
  disassembly superseded both — see §7.
- **`sys_pakPriority` dropped entirely.** kcdx's resolution is **independent of
  `sys_pakPriority`** — the overlay decision sits ABOVE the engine's per-mode
  existence-test table. The CVar is not a dependency, not a fallback, not
  mentioned as a mechanism. (A dev-mode CVar a game update could silently change
  is an unacceptable durability risk for the most crucial author surface.)
- **Seam install timing settled** (§8) — the seam installs inside the
  already-shipping ready-bracket; a build-step-1 probe confirms the ordering.

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

**Top-level architecture decision: kcdx OWNS asset resolution.** The same way
kcdx replaced the native mod loader (kcdx is now the mod loader), kcdx replaces
the engine's asset-resolution decision and runs it to kcdx's own spec. It does
NOT negotiate with the engine's `sys_pakPriority` modes or its loose-file search
behavior — it sits ABOVE them and decides which bytes win. The single replaced
function (§7) is the entire ownership surface; everything else (the pak mount
machinery, the read path, the existence leaves) is the engine's and is reused by
calling through. This is what makes startup-asset mods possible (kcdx resolves
before the game's first asset read, §8) and what makes the system durable against
a game update changing a mode CVar.

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

### §4.3 Transparent staging — the author never sees the asset class

The author's rule is uniform: drop the file in `assets/`, declare what it
replaces. Because **both asset classes route through the one resolution seam**
(§7), kcdx resolves a declared overlay to the plugin's loose `assets/` file
directly for BOTH classes — the seam returns kcdx's path, and the engine's
unmodified read family then serves kcdx's bytes by the handle the resolved path
produces. The author never needs to know which class is which; exposing that
would fail the disassembler test (`cornerstones.md`).

**One build-gated unknown remains here** (§9): whether a declared overlay served
straight from the plugin's `assets/` dir resolves end-to-end for the
handle-consumed class through the replaced seam, or whether that class needs kcdx
to stage the file under a kcdx-managed root first. The memory-mapped class is
verified (live, §7). The handle-consumed resolution + any staging lifecycle is
**pinned by the build's first probe (§9), not guessed in this doc.** The author's
surface (§5) is class-agnostic regardless of which the probe shows — staging, if
needed, is internal.

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

## §7 RE evidence — the verified seam (the design rests on these facts)

Every fact below is verified against WHGame.dll (`release_1_5_1164953_841`), from
the 5-front asset-resolution disassembly (fresh Ghidra + capstone + live probes,
2026-06-02); the provenance is the verified value + its evidence tier per
`reverse-engineering.md`. The seed entities are already authored (no new seed row
— `anti-patterns.md` AP18).

**The one structural fact that drives the whole design:** resolution funnels
through a SINGLE method that runs BEFORE any disk/pak touch, and the loose-vs-pak
choice is baked into the handle at open-time — there is no per-read resolution. So
owning that one method owns resolution for BOTH asset classes.

- **The resolution seam — REPLACE this.** `CCryPak::AdjustFileName` (kcdx_id
  **152**, RVA `0x6205C`, ICryPak vtable **slot 1**, offset `+0x8`) is the
  resolution-decision root: every by-name file consumer (FOpen, exists, get-size,
  enumerate, delete, copy — 9+ slots) calls `(*(*pCryPak+8))(this, name, buf, …)`
  to resolve a vpath to a concrete path STRING before any disk/pak touch. kcdx
  replaces this method's body (Around/Replace via `hook_chain::AddCEngine`): on a
  declared-overlay HIT it returns kcdx's path; on a MISS it falls through to the
  engine precedence. Owning it makes both classes obey kcdx, **independent of
  `sys_pakPriority`**, because the overlay check sits ABOVE the per-mode
  existence-test table. *Evidence:* fresh Ghidra decompile of slot 1 + the 9+
  consumer-slot decompiles confirming they all call `*(vtable+0x8)`.
- **Call-through leaves (REUSE — do not reimplement), invoked from inside the
  kcdx replacement on an overlay miss so stock content resolves unchanged:**
  - **Pak membership** `CCryPak::IsFileInPak` (kcdx_id **153**, RVA `0x631F0`) —
    binary-searches the loaded-pak directory index (`this+0x120..0x128`),
    origin-agnostic (any mounted pak, stock or kcdx). The fall-through on an
    overlay miss → every stock pak asset (incl. a stock Nexus/Workshop pak mod,
    US-7) resolves exactly as today. *Evidence:* fresh Ghidra decompile.
  - **Disk existence** `CCryPak::DoesFileExistOnDisk` (kcdx_id **154**, RVA
    `0x9C9CB4`) — the OS file-attribute query for the loose-disk arm. *Evidence:*
    fresh Ghidra decompile.
  - **Root-prefix normalizer** `CCryPak::AdjustFileName_RootPrefix` (kcdx_id
    **155**, RVA `0x621BC`) — prepends the game data root (`this+0x188`) or matches
    a recognized root for a bare/relative vpath, before the existence checks. kcdx
    reproduces or calls through it so stock vpaths still root correctly.
    *Evidence:* fresh Ghidra decompile.
- **The reused engine machinery (touch nothing):** the entire handle-consume read
  family (FRead slot 40 `0x51CD00`, FSeek/FTell/FEof/FWrite/FClose — pure
  handle-tag dispatch, zero resolution logic); `CCryFile::Open` (kcdx_id 136 — a
  thin shell over FOpen, adds no resolution); the pak mount/archive machinery
  (OpenPack / OpenPacks / the ZipDir central-directory parser / rank-insert — kcdx
  drives the engine to mount its own paks via the engine's own OpenPack, it does
  not reimplement the zip index). `CCryPak::FOpen` (kcdx_id 131, slot 36,
  `0x4614A0`) is NOT hooked — once slot 1 is owned, a FOpen hook is redundant and
  WRONG: it would miss the 9 other by-name surfaces an overlay can be reached
  through (existence/enumerate/size). *Evidence:* the slot table (front 1) + the
  FRead decompile (front 3) + real-file PKZIP verification (front 5).
- **Override is confirmed live for the memory-mapped class** at the plugin's own
  `assets/` path. *Evidence:* live probe — a `.dds` overlay from the plugin's
  `assets/` dir rendered in-game (the menu logo visibly changed). The seam was
  verified the RIGHT mechanism; the handle-consumed class's end-to-end resolution
  is the §9 build probe.
- **`sys_pakPriority` is NOT a mechanism here.** The CVar's modes (files>paks /
  paks>files / paks-only / mods-prefix) govern the ENGINE's own precedence among
  the existence leaves. kcdx's overlay decision sits ABOVE that table, so the seam
  owns resolution regardless of the CVar's value — kcdx neither sets nor depends
  on it. (A dev-mode CVar a game update could change is an unacceptable durability
  risk for this surface; the seam-replace removes the dependency entirely.)

(The detailed RE write-ups + probe findings live in the private RE tree; this doc
states the verified facts the design rests on. The public author-facing guide
restates only the *what*, never the RE provenance — `public-private-boundary.md`.)

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

The asset-resolution seam installs **inside this same window** — kcdx installs the
`AdjustFileName` replacement (and resolves the refdb facts it needs — ids 152–155)
**before it signals `g_kcdxReadyEvent`**. So the game-init thread blocks at
`ModManager_ctor` until the seam is live; resolution is owned before the game
proceeds past the ctor. No new wait-mechanism is built — the seam slots into the
one that already ships.

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

The simple-replacement case (US-1, memory-mapped) is **verified live**. Two
mechanisms rest on unverified facts the build resolves by probe FIRST, per
`.claude/rules/results-driven.md` — the design does not guess them, and the
dependent surface is built to whatever the probe resolves:

1. **Seam install ordering** (§8) — does `ModManager_ctor` fire before the first
   asset read? **Build step 1's probe.** Settles WHERE the seam installs; a
   falsifying outcome is a surfaced design fork.
2. **Handle-consumed end-to-end resolution + staging** — does a declared overlay
   served from the plugin's `assets/` dir resolve end-to-end for the
   handle-consumed class (`.lua`/`.xml`) through the replaced seam, or does that
   class need kcdx to stage the file under a kcdx-managed root first? The
   memory-mapped class is verified; this class is the probe. Settles §4.3's
   staging question and the path `get_by_path`/`get_by_name` hand back. The
   staging lifecycle (ephemeral-regenerate vs tracked-invalidate) is pinned AFTER
   this probe settles the mechanism, never designed on an unverified resolution.

These are the build's opening probes (ordered before the surfaces that depend on
them — `.claude/rules/incremental-delivery.md`), not design forks.

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
| **The ownership mechanism** | **REPLACE `CCryPak::AdjustFileName` (slot 1, id 152)** — kcdx owns the resolution decision, reusing the pak/disk/normalizer leaves (153/154/155) + read family + mount machinery by calling through. **Rejected:** hooking `CCryPak::FOpen` (slot 36) — misses the 9 other by-name surfaces an overlay reaches through; the prior design's mechanism, superseded by the 5-front RE. |
| **`sys_pakPriority` as a mechanism** | **Rejected — dropped entirely.** kcdx's overlay decision sits ABOVE the engine's per-mode existence table, so resolution is independent of the CVar. A dev-mode CVar a game update could silently change is an unacceptable durability risk; the seam-replace removes the dependency. (Non-negotiable.) |
| **Seam install timing** | Install the seam inside the already-shipping ready-bracket (before `SetEvent(g_kcdxReadyEvent)`), so it is live before the first asset read; a build-step-1 probe confirms the `ModManager_ctor`-vs-first-read ordering, a falsifying outcome is surfaced. **Rejected:** wiring it in without confirming the ordering (the unverified-timing assumption that caused the `hits=0` result); rejected: building a new wait-mechanism (the ready-bracket already ships). |
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
