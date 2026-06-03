# Asset-resolution ownership — research synthesis (5-front disassembly)

Captured 2026-06-02 from the asset-resolution-research workflow (run wf_f6cff627-188; 5 parallel
Ghidra fronts + synthesis). The decision-grade answer to: can kcdx OWN asset resolution? The
front maps are the sibling front*.md / _*.txt files in this dir; this is the consolidated synthesis.

Verified. The FRead decompile (`_readpath_decomp.txt` lines 97-139) confirms front 3's central claim exactly: `FUN_18051cd00(longlong *param_1, FILE *param_2, longlong *param_3)`, the dispatch bound check `puVar6 < puVar2` (handle-index vs pak-handle count), pak arm `FUN_18051ce40`, OS arm `FUN_1804d7ab4` (the CRT fread leaf). Front 1's slot table confirms slot 1 = AdjustFileName resolver at 0x46205C, slot 36 = FOpen at 0x4614A0.

All three structural pillars verify cleanly against the decompile dumps. I have enough to synthesize a decision-grade answer. One note worth surfacing: the MECHANISM-CONFIRMED doc shows a prior live test already proved `sys_pakPriority=0` works for both classes — but the brief explicitly asks for resolution ownership *independent of sys_pakPriority*, which is the slot-1 replace, not the cvar flip. That tension is a key synthesis point.

I have what I need. Producing the consolidated synthesis.

---

# kcdx-owned asset resolution — consolidated synthesis (5-front map)

Image base `0x180000000`; WHGame.dll `release_1_5_1164953_841`. CCryPak vtable @ VA `0x183A95FA8`, reached via `*(gEnv+0x50)` (gEnv id 1010, pCryPak id 132). Every load-bearing claim below is anchored to a front's cited decompile/slot/real-file evidence and re-verified this session against the underlying dumps (`front4_resolution_decision_tree.md`, `_readpath_decomp.txt:97-139`, `front1-full-vtable-surface.md:37/70`).

## The one structural fact that drives all four answers

Resolution funnels through a SINGLE method that runs BEFORE any disk/pak touch, and the loose-vs-pak choice is baked into the handle at open-time — there is no per-read resolution:

- **`CCryPak::AdjustFileName` = vtable slot 1 (+0x8), RVA `0x6205C` (`FUN_18046205c`)** is the decision root. Every by-name vtable consumer calls `(**(*this+8))(this, name, buf, …)` to resolve the vpath first (front 1: opens 35/36/38, existence 45/67/70, sizes 92/93, attrs 68, folder 13, enumerate 14, delete 49, copy 52, CRC 81/82 — all verified in their decompiles). It returns a path STRING, not a handle.
- **`CCryPak::FOpen` = slot 36 (+0x120), RVA `0x4614A0`** calls slot 1, then mints a TAGGED handle: a small `index+1` pak-pseudo-handle (into the pak-handle vector at `[this+0x40]`) OR a real OS `FILE*`. The read family (`FRead` slot 40 `0x51CD00`, `FSeek` slot 38, `FEof/FTell` slot 39, `FWrite` slot 41, `FClose` slot 55) dispatches purely on `handleIdx < pakHandleCount` — verified in `_readpath_decomp.txt:113-132` (OS arm `FUN_1804d7ab4` is literally `fread(buf,sz,n,FILE*)`). No read-path resolution exists.

Consequence: **owning slot 1 owns resolution for BOTH asset classes** (memory-mapped and handle-consumed), because both classes' opens route through slot 1 → FOpen, and the handle they get is whatever slot 1's resolved path makes FOpen mint. This is the actionable core of every answer below.

---

## 1. SALVAGEABLE — what is reusable vs must be replaced

**REUSABLE (keep, call through, do not reimplement):**

- **The entire handle-consume read family** (front 3, VERIFIED): FRead/FSeek/FTell/FEof/FWrite/FClose + the handle-IO block slots 39–59 are pure mechanical handle-tag dispatch with zero resolution logic. Reusable verbatim IF kcdx honors the handle-tag contract (small `index+1` = pak entry in `[this+0x40]`; anything else = a real `FILE*`). The OS arm is plain CRT `fread`/`fopen_s`, so a kcdx loose override returned as a real `FILE*` needs zero engine-private state.
- **`CCryFile::Open`** (`FUN_1804605bc`, RVA `0x4605BC`, seed id 136) — adds ZERO resolution; routes through `ICryPak::FOpen` (vtable+0x120) and stores the tagged handle. A thin buffering shell over slot 36; reusable.
- **The pak mount/archive machinery** (front 2, VERIFIED): OpenPack (slot 6 `0xDA4E5C` / slot 7 `0x193CB14`), OpenPacks glob (slot 9 `0x4D9BB0` / slot 10 `0x197C598` → glob worker `0x4D9C4C`), the archive factory (slot 72 `0x4D5580`), the **ZipDir Central-Directory parser** (`0x4D6E70`/`0x4D6C18`/`0x8B8BA8`/`0x8B8D6C`), and the **rank-insert** (`0x4D70A4`). kcdx drives the engine to mount its own paks exactly as it mounts stock ones — do NOT reimplement the zip/index/bisection.
- **The two existence leaves** the resolver calls: **PAK membership** `FUN_1804631f0` (RVA `0x631F0` — binary-searches the loaded-pak dir index at `[this+0x120..0x128]`, origin-agnostic) and **DISK existence** `FUN_1819c9cb4` (RVA `0x9C9CB4` — OS attr query via vtable+0x228). Single-purpose, callable-through.
- **The `this`-struct resolver model** (front 4): search-path vector `+0x198/+0x1a0`, alias table `+0x1b0/+0x1b8` (a ready-made prefix-substitution redirect, struct `{from, fromLen, to, toLen}`), loaded-pak array `+0x120` (stride 0x38), data root `+0x188` (`this[0x31]`), pakPriority cvar `+0x228 → +0x20`.
- **Search-path registrars** (front 1): AddMod (slot 19 `0x19AF1A8`), RemoveMod (20), GetMod (21) — reusable as "register a root" IF kcdx keeps the search-path vector.
- **The stock pak FORMAT reader is not even engine-private** (front 5, VERIFIED on real files): both real Nexus paks are standard PKZIP (`PK\x03\x04` at byte 0, STORED+DEFLATE per entry, no zip64, unsigned CDR). kcdx could use any standard zip decoder; the engine's ZipDir is reusable but not mandatory.

**MUST BE REPLACED (the only thing kcdx owns):** the **decision logic inside slot 1** — the precedence rule that picks which bytes win. That single function is the whole replacement surface.

---

## 2. REPLACE — full or partial?

**PARTIAL replacement: replace exactly ONE function (slot 1's body), reuse everything else.** Structural reason: resolution is a single chokepoint (slot 1) that every by-name surface and both asset classes funnel through, and it returns a path string rather than a handle — so replacing it does not touch handle lifetime, the read path, the mount machinery, or the zip index. A full vtable swap is unwarranted (front 1); reimplementing the ZipDir parser/index/bisection is wasted work (front 2); the read family needs no change (front 3). The clean seam is "own the WHICH-bytes decision, reuse the HOW-to-read-them and HOW-to-mount-them machinery."

---

## 3. HOOK-OR-REPLACE-WHAT — the exact seam (the actionable core)

To own resolution for BOTH asset classes independent of `sys_pakPriority`:

**REPLACE (one seam):**
- **Slot 1 `CCryPak::AdjustFileName`, RVA `0x6205C`** — Around/Replace. This is where "vpath → which concrete path" is decided, upstream of FOpen's handle minting. Owning it makes both classes obey kcdx: the memory-mapped class (resolved path → FOpen mints whatever handle) and the handle-consumed class (same path → same FOpen → the unmodified FRead family serves kcdx's bytes by the handle tag). Independent of `sys_pakPriority` because kcdx's overlay check sits ABOVE the per-mode existence-test table — kcdx decides before the engine's mode-gated DISK/PAK ordering is ever consulted.

**REUSE / CALL-THROUGH from inside the kcdx slot-1 body (so stock content still resolves):**
- `FUN_1804631f0` (PAK membership, `0x631F0`) — fall through to it on a kcdx-overlay miss → every stock pak asset resolves exactly as today.
- `FUN_1819c9cb4` (DISK existence, `0x9C9CB4`) — for the loose-disk arm.
- The leaf normalizer's root-prefix logic (`FUN_1804621bc`, `0x621BC`) — kcdx must reproduce or call through for the recognized-root / data-root prepend so stock vpaths still root correctly.

**DO NOT hook/replace:** FOpen (slot 36) — redundant once slot 1 is owned; the read family (slots 38–59) — pure dispatch, redundant; AddMod / the alias walker — orthogonal to the decision; the mount path — kcdx drives it via the engine's own OpenPack, it does not hook it.

**The single named seam: REPLACE slot 1 (`0x6205C`); REUSE the pak leaf (`0x631F0`) + disk leaf (`0x9C9CB4`) + normalizer (`0x621BC`) by calling through; touch nothing else.**

A FOpen-only (slot 36) hook is the surgical subset but is WRONG — front 1 proved it misses the 9 other by-name surfaces (an overlay opened via slot 35/38 or probed via existence slot 67 would be invisible to a FOpen-only hook). Slot 1 is the correct, complete seam.

---

## 4. BACKWARD-COMPAT — loading a STOCK Nexus/Workshop pak unchanged

**The compatibility contract** (front 5, VERIFIED on `easytoseeherbs` + `MH_Rebalanced_Sharpening_X3`). A stock pak mod loads with ZERO author changes iff the owned resolver honors:

1. **Container = standard PKZIP.** `PK\x03\x04` at byte 0 (NO proprietary CryPak header prepended), STORED (`m=0`) and DEFLATE (`m=8`) per-entry, no zip64, EOCD-comment-tolerant, unsigned CDR. (The Warhorse wiki "uncompressed ZIP" claim is FALSE — both real paks DEFLATE; the herbs pak is 9.4 MB compressed from 44.7 MB.)
2. **Manifest = lenient `<kcd_mod><info>` UTF-8 XML, only `<modid>` required**; all other fields (name/description/author/version/modifies_level/created_on/dependencies) optional and freeform. `modid` (from the manifest, NOT the dir name) is the mount identity key.
3. **Each zip entry name IS the game vpath** — forward-slashed, root-relative to the game data namespace (no leading `Data/`; the on-disk `Data/<mod>.pak` wrapper dir is the mount location). The vpath collision with a stock asset IS the override.
4. **Enumeration/order:** `mods/mod_order.txt` lists modids, file-order = mount-order (already kcdx-managed).

**How the owned resolver honors it:** stock paks register their content INTO the loaded-pak array `[this+0x120..0x128]` via the engine's OpenPack path (or kcdx's init-cycle takeover, which already walks Workshop+mods — see project memory `init-cycle-ownership`). The kcdx slot-1 replacement, on a miss in its own overlay map, **falls through to `FUN_1804631f0`** (pak membership over that same array, origin-agnostic). This makes kcdx a strict SUPERSET: kcdx overlays checked first, then the engine's pak array for everything else → every stock-pak asset resolves exactly as today. Because the format is entirely standard and kcdx reuses the mount + index machinery, nothing in the stock format is kcdx-specific and the author changes nothing.

---

## kcdx-owned resolution design sketch (for the /design revision)

**Seam:** Replace CCryPak vtable slot 1 (`AdjustFileName`, `0x6205C`) with a kcdx resolver. Keep FOpen (36), the read family (38–59), the mount path (6/7/9/10/72), and the zip index untouched.

**Data flow (vpath in → kcdx decides → bytes out):**
```
caller → FOpen (slot 36, unchanged)
           └─ calls slot 1  ← KCDX-OWNED
                ├─ normalize vpath (reuse FUN_1804621bc 0x621BC: recognized-root / data-root prefix, alias substitution)
                ├─ KCDX OVERLAY CHECK  ← the new logic, sits ABOVE engine precedence
                │     hit  → return the kcdx-resolved concrete path
                │              (loose file → real FILE* ; or a kcdx-mounted pak entry)
                └─ miss → FALL THROUGH to engine precedence:
                          PAK membership  FUN_1804631f0 (0x631F0)   [stock paks]
                          DISK existence  FUN_1819c9cb4 (0x9C9CB4)  [stock loose]
         FOpen mints the handle from the returned path (pak-pseudo-handle OR real FILE*)
         read family dispatches on the handle tag (unchanged) → bytes out
```
Both asset classes are covered by one seam because both route vpath→slot1→FOpen, and the handle-tag contract (small `index+1` = pak; pointer = `FILE*`) is what the reused read family obeys — kcdx must preserve that contract for any handle it causes FOpen to mint. Independence from `sys_pakPriority` comes from placing the kcdx overlay check ABOVE the mode-gated existence table, so kcdx wins at the default mode 2 (PAK-first) without flipping the cvar.

**Relationship to the already-proven cvar mechanism (surface to the user — this is a design fork, not a settled fact):** A prior live test (`MECHANISM-CONFIRMED-pakpriority-loose.md`, VERIFIED end-to-end at 16:22) proved that `sys_pakPriority=0` + staging an overlay at `<game>/Data/<vpath>` already wins natively for BOTH classes, with NO hook — the engine's own loose-first search does it. The cvar registers `VF_NULL` (freely cfg-settable; the "published pins it to 2" claim is FALSIFIED). This is a real fork the /design revision must resolve: **(A) the slot-1 replace** (scoped per-overlay ownership, `sys_pakPriority`-independent, owns the by-name reference path too) vs **(B) the cvar flip + data-root staging** (zero engine-hook, but makes ALL loose files in the install win — a broader, global contract — and still needs a staging tree + the FOpen/by-name reference path handled separately). The brief asks for ownership *independent of sys_pakPriority*, which points at (A); but (B) is live-proven and simpler. Recommendation: (A) as the resolution seam, because it owns precedence without a global side effect and is the single point that also serves the by-name reference path — but this is the user's call.

---

## OPEN QUESTIONS / follow-up probes (honest gaps the research did NOT settle)

1. **`FUN_1804631f0` callable-through ABI from a kcdx slot-1.** Front 4 flags NEEDS-LIVE-CONFIRM: that a kcdx slot-1 replacement calling through to `FUN_1804631f0`/`FUN_1819c9cb4` preserves stock-pak resolution end-to-end. The leaf takes a thread lock (`+0x100`, recursive) and writes the owning pak to `*param_4` — its exact in-replacement call ABI (param layout, lock re-entrancy from a hooked context) is decompiled-shape only, not live-verified. **Probe owed before building the call-through.**
2. **Bit 28 (`0x10000000`) asset classes.** The ONLY path that reaches loose disk for a search-path entry at the default mode 2. Static caller scan did not show common asset opens setting it (front 4). **Needs a live readback** of which classes (if any) set it — determines whether the engine's own mode-2 path ever serves loose, which bears on the (A)-vs-(B) fork.
3. **Handle-tag contract under an Around-FOpen / kcdx-minted FILE\*.** Front 3 NEEDS-LIVE-CONFIRM: that an Around-FOpen returning a kcdx `fopen` FILE* makes the unmodified FRead serve a substitute asset (HIGH expected — it is the OS-arm mode-0 already exercises). If kcdx mints handles, this contract must hold. **One probe closes it.**
4. **Slot 72 / OpenPack mount ABI for a kcdx-driven pak** (front 2 NEEDS-LIVE-CONFIRM): that a kcdx-driven OpenPack from a post-init hook actually mounts and wins precedence, and exactly where in the rank band (bit-10 flag semantics) a kcdx mount lands. Relevant only if kcdx mounts its own pak rather than serving loose.
5. **The alias table's runtime CONTENTS** (front 4): layout verified, populated entries are runtime state — a live GetMod-style readback of `[this+0x1b0..0x1b8]` would enumerate them. Relevant if kcdx reuses the alias mechanism for redirects.
6. **Slot 71 vs slot 72 mount-entry reconciliation.** Front 1 names slot 71 (`0x7AD468`) as OpenPack/mount; front 2 names slot 6/7 + slot 72 (`0x4D5580`) as the archive factory. These are different slots with overlapping "mount" descriptions — **a follow-up decompile should reconcile the exact slot→role for the mount entry kcdx would drive** (not blocking the slot-1 seam, but needed if kcdx mounts its own pak).
7. **zip64 / data-descriptor pak tolerance** (front 5): neither real mod uses them, so engine tolerance is inferred-needs-confirm. Low priority (no shipped mod hits it) but a future mod could.

---

## AP18 seed-row candidates (consolidated; FLAGGED, NOT written — need user sign-off per entry)

Already seeded: FOpen (id 131, slot 36), gEnv_pCryPak (id 132), CCryFile::Open (id 136 prose), gEnv (id 1010).

**Primary (the seam kcdx replaces):**
- `CCryPak::AdjustFileName` resolver — `FUN_18046205c`, RVA `0x6205C`, slot 1 (+0x8). The decision root. (Fronts 1, 4.)

**Secondary (only if the kcdx resolver calls them directly):**
- PAK membership leaf — `FUN_1804631f0`, RVA `0x631F0`. (Fronts 1, 4.)
- DISK existence leaf — `FUN_1819c9cb4`, RVA `0x9C9CB4`. (Front 4.)
- Leaf normalizer — `FUN_1804621bc`, RVA `0x621BC`. (Front 4.)
- Alias walker — `FUN_180462664`, RVA `0x462664`; mode-3 gate — `FUN_18241ad60`, RVA `0x241AD60`. (Front 4.)

**Mount path (only for a kcdx-mounts-its-own-pak feature):**
- OpenPack — `FUN_180da4e5c`, RVA `0xDA4E5C`, slot 6; auto-bind-root `FUN_18193cb14`, RVA `0x193CB14`, slot 7. (Front 2.)
- OpenPacks glob worker — `FUN_1804d9c4c`, RVA `0x4D9C4C`, via slot 9 `0x4D9BB0` / slot 10 `0x197C598`. (Front 2.)
- Archive factory — `FUN_1804d5580`, RVA `0x4D5580`, slot 72; rank-insert — `FUN_1804d70a4`, RVA `0x4D70A4`; per-part mount leaf — `FUN_1804d526c`, RVA `0x4D526C`. (Front 2.)

**Read-path (likely NOT needed — synthesis points at slot 1 as the only seam; flagged for completeness):**
- FRead — `FUN_18051cd00`, RVA `0x51CD00`, slot 40. CCryFile::Open — `FUN_1804605bc`, RVA `0x4605BC` (referenced only in id 136 prose today). Secondary: FSeek `0x461304`, FEof/FTell `0x51E1F8`, FReadRaw `0x4618B4`, OS-arm fread leaf `0x4D7AB4`. (Front 3.)

**Registrars (only for a paked-mod-root / mechanism-2 feature):**
- AddMod — `FUN_1819af1a8`, RVA `0x19AF1A8`, slot 19; RemoveMod slot 20 `0x241CE08`; GetMod slot 21 `0x241A390`. (Fronts 1, 4.)

Front 5 introduced NO seed candidates (file-format dissection, no game-binary entity).

---

## Confidence map (VERIFIED = decompiled/real-file; INFERRED = needs-confirm)

| Claim | Confidence | Evidence |
|---|---|---|
| Slot 1 (`0x6205C`) is the single resolution chokepoint every by-name surface routes through | VERIFIED | front 1 decompiles of 9+ consumer slots; front 4 full tree |
| Full slot-1 decision tree (backward search-path loop, per-mode existence table, fallthrough, flag-bit decoder) | VERIFIED | front 4, all tier-2 reuse; re-read this session |
| FOpen (slot 36 `0x4614A0`) mints a tagged handle; read family dispatches on the tag only; no per-read resolution | VERIFIED | front 3; `_readpath_decomp.txt:113-132` (dispatch bound + CRT fread OS arm) |
| Read family + CCryFile reusable verbatim; OS arm = plain CRT `fread`/`fopen_s` | VERIFIED | front 3 decompiles |
| Pak mount path (6/7/9/10/72), ZipDir CDR parser, rank-insert; mount order = resolution precedence (back-to-front, bit-10-skip band) | VERIFIED | front 2 decompiles + CryPak.cpp source-path strings |
| Stock pak = standard PKZIP (STORED+DEFLATE, no zip64, unsigned CDR); manifest `<kcd_mod>` only `<modid>` required; entry name = vpath | VERIFIED | front 5, real on-disk bytes (2 Nexus mods) |
| `sys_pakPriority` is `VF_NULL` / freely cfg-settable; pakPriority=0 + data-root staging wins for both classes natively (no hook) | VERIFIED | `MECHANISM-CONFIRMED-pakpriority-loose.md` (live test 16:22 + cvar-reg RE) |
| 102-slot vtable extent; 60 role-verified slots | VERIFIED | front 1 (`_front1_surface_raw.txt`) |
| A kcdx slot-1 calling through to `0x631F0`/`0x9C9CB4` preserves stock resolution end-to-end | INFERRED-needs-confirm | front 4; live readback owed (OQ #1) |
| The leaf-call ABI / lock re-entrancy of `0x631F0` from a hooked slot-1 context | INFERRED-needs-confirm | decompiled shape only; OQ #1 |
| Which asset classes set bit 28 (only mode-2 loose-disk path) | INFERRED-needs-confirm | static scan showed none; OQ #2 |
| An Around-FOpen returning a kcdx FILE* makes unmodified FRead serve the substitute | INFERRED-needs-confirm (HIGH) | front 3; OQ #3 |
| A kcdx-driven OpenPack mounts + wins precedence; bit-10 landing position | INFERRED-needs-confirm | front 2; OQ #4 |
| Slot 71 (`0x7AD468`, front 1) vs slot 72 (`0x4D5580`, front 2) mount-entry role | INFERRED-needs-reconcile | two fronts, overlapping descriptions; OQ #6 |
| The ~42 thin-accessor/stub slots; alias-table runtime contents; zip64/data-descriptor tolerance | INFERRED-needs-confirm | fronts 1/4/5; low priority |

**Bottom line for the /design revision:** the seam is a single-function replacement — slot 1 `AdjustFileName` (`0x6205C`), reusing the pak leaf (`0x631F0`), disk leaf (`0x9C9CB4`), normalizer (`0x621BC`), the read family, and the mount machinery. Backward-compat is free (kcdx is a superset that falls through to the origin-agnostic pak array). The open design fork the user must settle is (A) slot-1 replace vs (B) the live-proven `sys_pakPriority=0` + staging mechanism; OQ #1–#3 are the probes that must close before building seam (A).