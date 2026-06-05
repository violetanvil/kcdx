# Probe 0.3 finding — the C++ `pe_helpers` surface already exposes section spans AND a disp32 follower (row 1, with one load-bearing caveat)

**Kind:** durable process-output (a captured scoping finding the Phase-3 engine-extension steps reuse).
**Trust level:** primary evidence — actual source signatures read at file:line this session (not an interpretation).
**Date:** 2026-06-05.
**Step:** maintainer-tool verification-engine Phase 0, step 0.3 ([ENG] probe — read the C++ `pe_helpers` surface). Scopes Phase 3 steps 1–3 (per-kind dispatch + the 5 non-function static checks + the live check) BEFORE they are built. READ-only; no `src/` change, no build.

## Question

Does the engine's existing PE-helper surface already expose what the Phase-3 C++ survival-checker extension needs — `.text` / `.data` / `.rdata` section spans (to scan/read those sections) + a RIP-relative `disp32` follower (for the `instruction_anchor` / `data_slot` derivation kinds) — or is that NEW C++ infra Phase 3 must build?

## Outcome → meaning map (pre-committed, flat)

| Outcome | Meaning | Verdict |
|---|---|---|
| `pe_helpers` exposes section spans + a disp32 follower | Phase 3 reuses both; per-kind checks are thin | **row 1** |
| Spans exist but NO disp32 follower (or vice versa) | Phase 3 builds the missing piece | row 2 |
| Neither exists — `pe_helpers` is narrower than assumed | Phase 3 has more infra to build; re-ground scope | row 3 (RETURN) |

## Verdict: **ROW 1** — both primitives EXIST. Phase 3 reuses them; the per-kind checks are thin.

`pe_helpers.h` is substantially richer than `survival.cpp` uses. `survival.cpp` (the existing checker) touches exactly ONE `pe_helpers` function — `RvaToFileOffsetOnDisk` — because the function-body-hash kind needs only "map an RVA to on-disk bytes." Every other primitive the 5 non-function kinds need is ALREADY in `pe_helpers` and already exercised in production by `patch_engine.cpp`'s anchor resolver. The Phase-3 per-kind checks are largely lift-and-adapt, not new infra.

**One load-bearing caveat that is row-1-with-a-named-delta, not row 2** (see §"The on-disk-vs-live caveat"): the rich primitives operate on a **LIVE loaded `ModuleView`** (live process memory via `GetModuleHandleW`), whereas `survival.cpp` deliberately reads the **raw ON-DISK file buffer**. The disp32-follow / section-scan LOGIC exists and is reusable; whether each Phase-3 check runs it against the live image or the on-disk buffer is a design call Phase 3 must settle (and surface), not new primitive code. This is captured in the gap list below so Phase 3 does not assume the existing live-image variants drop in unchanged.

---

## DELIVERABLE 1 — the PE-helper surface that EXISTS (actual signatures, file:line cites)

All in namespace `kcdx::pe`, declared in `src/pe_helpers.h`, defined in `src/pe_helpers.cpp`.

**Module open (the entry point for the live-image primitives):**
- `bool OpenModule(const wchar_t* moduleName, ModuleView& out)` — `pe_helpers.h:19` / `.cpp:8`. Resolves a loaded module via `GetModuleHandleW`; fills `ModuleView{ HMODULE base; const uint8_t* baseBytes; size_t size; PIMAGE_NT_HEADERS nt; }` (`pe_helpers.h:10-15`). **This is a LIVE-IMAGE view** — `baseBytes` is the relocated in-process image, not the on-disk file.

**Section spans (`.text` / `.rdata` / `.data`):**
- `std::vector<SectionView> Sections(const ModuleView& m)` — `pe_helpers.h:29` / `.cpp:24`. Returns every section as `SectionView{ std::string name; const uint8_t* data; size_t size; uint32_t rva; uint32_t characteristics; }` (`pe_helpers.h:21-27`). `name` is the literal section name (`.text`, `.rdata`, …); `data`/`size` are the span; `rva` is the section RVA; `characteristics` is the IMAGE_SCN flags.
- `std::vector<SectionView> ExecutableSections(const ModuleView& m)` — `pe_helpers.h:31` / `.cpp:42`. Filters `Sections` to `IMAGE_SCN_MEM_EXECUTE` (= `.text`-class).
- `std::vector<SectionView> ReadOnlyDataSections(const ModuleView& m)` — `pe_helpers.h:32` / `.cpp:52`. Filters to read-and-NOT-execute (= `.rdata`-class).
  - NOTE: there is **no `WritableDataSections` / `.data` accessor**. `.data` is read-write, so it is filtered OUT of both `ExecutableSections` and `ReadOnlyDataSections`. The raw `Sections(m)` returns it (it carries `.data` by name + characteristics), but no convenience predicate isolates it. This is a small named gap (see gap list) — trivially closed by a `WritableDataSections` predicate or a name match on `Sections`.

**`.rdata` string search (string_anchor):**
- `std::vector<uintptr_t> FindCStringsIn(const std::vector<SectionView>& sections, std::string_view literal)` — `pe_helpers.h:36` / `.cpp:64`. Finds a null-terminated literal in the given sections; returns the **absolute VA** of each occurrence. Exactly the string_anchor "is the literal present in `.rdata`" primitive.

**RIP-relative disp32 follower (instruction_anchor / data_slot) — THE KEY PRIMITIVE:**
- `std::vector<uintptr_t> FindLeaXrefsTo(const ModuleView& m, uintptr_t targetVA)` — `pe_helpers.h:42` / `.cpp:84`. Scans executable sections for the 7-byte `48 8D <modrm> rel32` (`lea r64, [rip+disp32]`) form, decodes `rel32`, computes `target = instrEnd + (intptr_t)rel` (`.cpp:96-100`), and returns the LEA starts whose computed target == `targetVA`. **This IS a RIP-relative disp32 follower** — it does the exact `instrEnd + disp32` arithmetic the `instruction_anchor` / `data_slot` kinds require (per `fingerprint-per-kind.md` §data_slot: "follow disp32 from instruction_anchor"). It is a REVERSE follower (find the instruction whose disp32 points AT a known target); a FORWARD follower (read the disp32 AT a known instruction RVA → compute the target slot) is the small delta Phase 3 adds — same arithmetic, opposite direction. The byte-decode recipe (`base[i+3..i+6]` little-endian → `int32_t`, `instrEnd = base+i+7`, `target = instrEnd + rel`) is the reusable wiring; `.cpp:91-100` is the literal skeleton.

**Function bounds (helps callsite / vtable_base land in `.text`, and instruction_anchor chain):**
- `bool FindFunctionBoundsViaPdata(const ModuleView& m, uintptr_t addressVA, uintptr_t& beginVA, uintptr_t& endVA)` — `pe_helpers.h:64` / `.cpp:167`. Resolves x64 function `[begin, end)` via `.pdata` RUNTIME_FUNCTION entries (binary search). Used to confirm a resolved VA lands inside a real `.text` function.

**On-disk RVA→file-offset (the function-hash kind, the only one `survival.cpp` uses today):**
- `bool RvaToFileOffsetOnDisk(const uint8_t* fileData, size_t fileSize, uint32_t rva, size_t length, size_t& fileOffsetOut)` — `pe_helpers.h:59` / `.cpp:109`. Maps an `[rva, rva+length)` span to its file offset in a **raw on-disk PE buffer** (parses the buffer's own DOS+NT+section headers; bounds-checks the whole span against `SizeOfRawData`). Fail-loud (returns false, never a silent zero). This is the ON-DISK analogue of the section walk; the live-image primitives above do NOT use it.

**Proof the rich primitives are production-exercised (not dead code):** `patch_engine.cpp`'s `ResolveAnchor` (`src/patch_engine.cpp:183-240`) ALREADY chains, for a string-anchored patch (`.cpp:192-213`): `ReadOnlyDataSections` → `FindCStringsIn` (find the `.rdata` literal, require unique) → `FindLeaXrefsTo` (follow the RIP-relative disp32 from `.text` to the string, require unique) → `FindFunctionBoundsViaPdata`. That is the `string_anchor` → `instruction_anchor` resolver chain `fingerprint-per-kind.md` describes, **already implemented and live**. `scan_engine.cpp` (`ScanAll`, `.cpp:29-41`) similarly does the `.text` AOB scan the `callsite` kind needs (via `patch::FindAllInBuffer` over `ExecutableSections`).

## DELIVERABLE 2 — section-span access

**YES.** The C++ side can get `.text` / `.rdata` spans directly (`ExecutableSections` / `ReadOnlyDataSections`), and `.data` via the raw `Sections(m)` (by name + characteristics) — with the one small gap that no `.data`-specific convenience predicate exists yet (gap G2). Each `SectionView` carries `{name, data, size, rva, characteristics}`, i.e. the full span + its RVA. Cite: `pe_helpers.h:21-32`, `.cpp:24-62`.

## DELIVERABLE 3 — disp32 follower

**YES, it exists and is reusable.** `FindLeaXrefsTo` (`pe_helpers.h:42` / `.cpp:84-107`) decodes a `lea r64,[rip+disp32]` and computes `instrEnd + disp32` — the exact RIP-relative arithmetic the `instruction_anchor` / `data_slot` kinds need, already in production via `patch_engine.cpp:199`. It is a REVERSE follower (target-known → find the instruction); the FORWARD direction the `data_slot` check wants (instruction-RVA-known → read disp32 → compute slot VA) is the same 4-byte-LE-decode + `instrEnd + rel` arithmetic, applied at a known site — a thin Phase-3 addition reusing `.cpp:96-100` verbatim, NOT new primitive infra. (Sibling probe 0.2 already validated this exact forward derivation in JS against the real binary: `0x0086AD99 → disp32 → 0x0492B8A8`.)

## DELIVERABLE 4 — the gap list (per Phase-3 check: needed primitive → EXISTS or NEW)

| Phase-3 check | Primitive it needs (per `fingerprint-per-kind.md`) | Status |
|---|---|---|
| **function** (hash) | RVA→bytes on the span | **EXISTS** — `RvaToFileOffsetOnDisk` (`survival.cpp` already uses it). |
| **callsite** (AOB re-match) | `.text` AOB scan, count hits (unique/zero/multi) | **EXISTS (logic)** — `ExecutableSections` + `patch::FindAllInBuffer` (`scan_engine.cpp:29-41`, `patch_engine.cpp:168-178`). NEW: an AOB+mask matcher with `?` wildcards IF `FindAllInBuffer`'s pattern form doesn't already carry masks (Phase 3 confirms `patch::Pattern`'s wildcard support — likely present, since notes carry `??`). |
| **string_anchor** (presence + optional unique-xref) | `.rdata` literal search; optional LEA-xref count | **EXISTS** — `ReadOnlyDataSections` + `FindCStringsIn`; `FindLeaXrefsTo` for the unique-xref assertion (`patch_engine.cpp:193-201` does both). |
| **vtable_base** (table-shape: read N qwords, each → `.text`) | read N qwords at an RVA; classify each as a `.text`-range pointer | **EXISTS (parts)** — `Sections`/`ExecutableSections` give the `.text` range to range-check against; reading N qwords at an RVA is `RvaToFileOffsetOnDisk` (on-disk) or direct `baseBytes + rva` (live). NEW: the small "is this qword a relocated `.text` pointer" classifier (a range test + relocation awareness) — thin, no new primitive. |
| **instruction_anchor** (resolver-chain re-derivation + shape match) | follow string-anchor → `.text` LEA (disp32) → expected instruction shape | **EXISTS (chain)** — `FindCStringsIn` + `FindLeaXrefsTo` IS this chain (`patch_engine.cpp:192-213`). NEW: the final "instruction-shape AOB match at the resolved site" assertion (an AOB compare at a VA — same matcher as callsite). |
| **data_slot** (derivation: follow disp32 from anchor, verify lands in `.data`) | FORWARD disp32 follow at a known instruction RVA → slot VA; confirm it's in `.data` | **EXISTS (arithmetic)** — the `instrEnd + disp32` decode is `FindLeaXrefsTo`'s body (`.cpp:96-100`). NEW: a small forward-direction helper (read disp32 at a given RVA, compute target) + the `.data` range check (needs the `.data` span — gap G2). Thin; reuses existing decode. |
| **live check** (the runtime/live-image survival pass, vs on-disk) | a LIVE `ModuleView` of WHGame.dll + the same scans against live memory | **EXISTS** — `OpenModule` + all the `ModuleView`-based primitives ARE the live-image surface (that's their native mode; `patch_engine`/`scan_engine` run them live). |

**Named gaps Phase 3 must build (all thin — adaptations, not new infrastructure):**
- **G1 — forward disp32 follower.** A `(fileOrImage, instructionRVA) → targetVA` helper reusing `FindLeaXrefsTo`'s 4-byte-LE decode (`.cpp:96-100`). For `data_slot` + the `instruction_anchor` MOV step. (The reverse follower exists; this is the same arithmetic forward.)
- **G2 — `.data` section accessor.** No `WritableDataSections` predicate exists; `.data` comes from raw `Sections(m)` by name/characteristics. Add a one-line predicate (mirror `ReadOnlyDataSections`, `.cpp:52-62`) for the `data_slot` "did we land in `.data`" check.
- **G3 — AOB+mask matcher for the anchor/callsite SHAPE asserts** IF `patch::Pattern` doesn't already carry `?` wildcards. Phase 3 step-1 confirms `patch::Pattern`/`FindAllInBuffer` wildcard support before deciding this is new vs reuse (the notes already store `??` patterns, strongly implying support — likely reuse, not new).
- **G4 — vtable_base qword-pointer classifier.** A "is this qword a relocated `.text`-range pointer" test (range-check against `ExecutableSections` + relocation handling). Thin; no new primitive.

### The on-disk-vs-live caveat (why this is row-1-with-a-delta, not row 2)

`survival.cpp` reads the **raw on-disk WHGame.dll file** (header doc `survival.h:11-16`: hashes the on-disk backing file so the recorded hash compares with zero normalization — live memory carries applied relocations and diverges). The rich `pe_helpers` primitives (`Sections`/`FindCStringsIn`/`FindLeaXrefsTo`/`FindFunctionBoundsViaPdata`) take a `ModuleView` = the **LIVE loaded image** (`OpenModule` → `GetModuleHandleW`). So the disp32-follow / section-scan LOGIC is fully present and reusable, but Phase 3 must decide, PER KIND, whether each non-function check runs against the on-disk buffer (matching the function-hash kind's normalization, but then the existing `ModuleView` helpers don't apply unchanged — they'd need on-disk variants parsing the file's own headers, the way `RvaToFileOffsetOnDisk` already does) or against the live image (the existing helpers drop in, but relocated `.data` pointers / live patches must be reasoned about — `fingerprint-per-kind.md` already flags that `.data` byte content is NOT stable, which is why data_slot is a derivation check not a hash). This on-disk-vs-live decision is a **Phase-3 design fork to surface to the user** (`design-authority.md`), NOT new primitive code and NOT a reason to re-scope the plan. The plan's "extend, not new-build" assumption holds: the primitives exist; the work is dispatch + per-kind adaptation + this one on-disk/live design call.

## DELIVERABLE 5 — outcome→map verdict

**Row 1** — `pe_helpers` already exposes section spans (`.text`/`.rdata` directly, `.data` via raw `Sections`) AND a RIP-relative disp32 follower (`FindLeaXrefsTo`), both production-exercised by `patch_engine.cpp` / `scan_engine.cpp`. The Phase-3 per-kind checks are thin: dispatch + per-kind logic that lifts the existing chain (string-find → disp32-follow → bounds) and AOB scan, plus the four named thin gaps (G1 forward-disp32, G2 `.data` predicate, G3 wildcard-matcher-confirm, G4 vtable qword classifier) and one design fork (on-disk vs live, per kind). Phase 3 steps 1–2 scope to dispatch + per-kind logic, NOT a from-scratch PE-infra build.

## DELIVERABLE 6 — finding file path

`_research/maintainer-tool-verification-engine/probe-0.3-pe-helpers-surface-finding.md` (this file).

## Surfaced to the caller (per `deferral-authority.md` / `design-authority.md` — not the probe's to decide)

- **The on-disk-vs-live design fork** (above) is a genuine Phase-3 design decision, surfaced here for the user, not resolved by the probe. It does not change the row-1 verdict (the plan's extend-not-new-build assumption holds) but Phase 3 step 1 must settle it before the per-kind checks are built.
- **No `src/` change made; no build run** (this was a read, per the step scope).
