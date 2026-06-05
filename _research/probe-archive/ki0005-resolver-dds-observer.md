# PROBE A/B (KI-0005) — does a re-opened vanilla `.dds` reach the asset resolver?

Captured 2026-06-04. The finding + the reusable observer wiring; the in-source
probe was removed after (working-artifacts.md no-residue). Reconstruct from this
recipe for the Phase-11 boot-asset serve confirmation, or any "does the engine
re-open asset X through the resolver" question.

## The question + verdict

Why does a `kcdx.assets` runtime `register`/`replace` overlay not SERVE in-game
for a re-opened vanilla vpath (cap-75 replaced `Libs/UI/Textures/KCDLogo.dds`; it
rendered vanilla; no resolver HIT)?

**VERDICT (PROBE B, dedup-free): the engine opens the menu logo EXACTLY ONCE per
session — at boot, BEFORE `plugin.lua` runs the register — then re-uses the loaded
GPU texture, never re-opening the FILE.** A save-load + two pause-menu opens
produced NO second logo open. Root cause: the Lua runtime verb runs after the
engine's boot asset open (the Lua VM is created in `CSystem::Init`, the same phase
that opens the logo), so take-effect="thereafter" can never reach a boot-cached
asset. The runtime store is NOT buggy — its one logo lookup was a correct
pre-register MISS. → Phase-11-gated (the DllMain Lua VM lets `plugin.lua` run
before the boot open); same trigger as before_game Lua hooks. KI-0005 closed.

## The observer recipe (reconstruct in src/asset_overlay.cpp)

Two stages. PROBE A first (deduped, both hooks); PROBE B refined it (un-deduped
for the target vpath) when the dedup masked the post-register open.

- **State** (file-scope anon namespace, near the `g_loggedFirst*` latches):
  `std::mutex g_probeA_mu; std::set<std::string> g_probeA_seen;`
  `std::atomic<uint32_t> g_probeB_logoSeq{0};` (+ `#include <mutex>`, `<set>`).
- **HOOK 1 `AdjustFileNameResolver`** + **HOOK 2 `FOpenLooseOverlay`**, right
  after `const std::string key = NormalizeVPath(pName);` (before the lookup):
  - filter `.dds` (`key.compare(n-4,4,".dds")==0`);
  - PROBE A (deduped): `if (g_probeA_seen.insert("h1:"+key).second)` /
    `"h2:"+key` — log `probeA_h{1,2}_dds` with `raw=pName`, `key`, and the runtime
    verdict from `asset_namespace::LookupRuntimeOverlay(key, disk, &winner)` →
    `rt=HIT|MISS`. The `h1:`/`h2:` prefix tells which hook the open routes through.
  - PROBE B (un-deduped, for ONE target vpath): for the specific vpath
    (`key == "libs/ui/textures/kcdlogo.dds"`), log EVERY open —
    `probeB_logo_open seq=<g_probeB_logoSeq.fetch_add(1,relaxed)+1>` + key + the
    `rt` verdict. Un-deduped so a post-register re-open shows (the dedup in
    PROBE A masked it).
- Observe-only: logs and falls through to the unchanged HIT/MISS logic. The
  `LookupRuntimeOverlay` call is a harmless read.

## Key facts established (cite KI-0005 closed)

- The resolver IS on the `.dds` open path: 20,361 distinct `.dds` reached HOOK 2
  (FOpen) on a save load; 0 reached HOOK 1 (AdjustFileName). Textures open through
  FOpen.
- The register-side key and the engine-open-side key are byte-identical
  (`libs/ui/textures/kcdlogo.dds`) — no normalization mismatch.
- The logo is opened ONCE (boot), never re-opened on a pause-menu display.
- `plugin.lua` (RunAll, `src/hooks.cpp:305`) fires from a game lifecycle hook —
  it needs the engine's Lua VM, created in `CSystem::Init` (after the boot open).
  The C++ `Plugin_Load` runs early (in `DiscoverAndLoad`, no VM) and the
  declarative sidecar parses early (data, no VM) — both pre-boot-open. Only the
  Lua runtime verb is late.

## Reuse for Phase 11

When FIX A brings the Lua VM up at DllMain (Phase 11), re-run PROBE B against a
boot asset after wiring an early Lua register: a `seq=1` (or early) open with
`rt=HIT` confirms the runtime overlay now wins the boot open. Until then, the
after-VM serve confirmation uses a gameplay/on-demand asset opened after
`plugin.lua` runs.

## DEAD END (2026-06-04) — the FOPEN observer count does NOT identify an after-VM serve vehicle

An attempt to confirm the in-game serve NOW (pre-Phase-11) by re-vehicling the
serve rows to a "real after-VM UI texture" FAILED, for a root reason worth not
repeating:

- The vehicles picked (`libs/ui/textures/apse/item.dds` for cap-75,
  `apse/attack_mode.dds` for comp-16) were chosen from the FOPEN observer's raw
  open-count ("5,542 `libs/ui/` `.dds` open on a save-load"). That count records
  OPEN-EVENTS, not whether each was a FIRST-EVER open vs. a GPU-cache refresh.
- Both vehicles turned out to be BOOT-CACHED atlases: the AP14 teaching warn
  (`runtime_overlay_boot_asset`) fired for `apse/item.dds` AND `kcdlogo.dds` on
  the `19-44-47` run, proving the engine opened them PRE-VM (`RecordBootOpen`
  populated the boot set before `plugin.lua`'s register). Boot-cached = the
  exact KI-0005 mechanism = cannot serve via a post-VM runtime register.
- The run produced ZERO real `overlay_resolved`/`overlay_opened` serve lines for
  any runtime vpath (the `g_loggedFirst*` one-shot markers never fired for a
  runtime hit) — no registered vpath served in-game, confirming the whole class
  is boot-cache-gated, not vehicle-tuning-gated.

**Lesson for the Phase-11 confirmation:** the after-VM vehicle must be proven
post-VM by INSTRUMENTATION, not inferred from an open-count. The correct probe
(when an early Lua slot exists OR to find a pre-Phase-11 vehicle): instrument the
resolver to log the FIRST vpath opened strictly AFTER `NotifyVmReady` fires
(check `asset_namespace::WasBootOpened(key) == false` at open time) — that vpath
is genuinely after-VM and non-boot-cached. Re-vehicle to a PROVEN such vpath, not
a guessed one. The serve rows (CAP-75-register-serve, COMP-16-serve-code) are
marked DEFERRED → Phase 11 in `test-plugins/README.md`, not PENDING — there is
no pre-Phase-11 in-game serve to confirm for a boot-cached target.
