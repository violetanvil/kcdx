# Localization-manager RE findings (2026-05-26)

Loc int-ID resolution sub-feature, step 2 (`parallel-ghidra-research.md` §6).
The GATING static unknown: locate `CLocalizedStringsManager` and its key→int-ID
table layout, so a runtime probe can dump key→int-ID pairs.

**Tier reached:** 5 (fresh Ghidra). Tiers 1-4 had nothing on localization
(no seed row, no prior dump, predecessor only forward-declares `ISystem`).
gEnv resolution NOT re-derived — reuse muyuanjin recipe per the task.

**Script:** `third-party-ghidra/ghidra_scripts/FindLocManager.java` (anchor-string
→ referencing-functions → decompile). Raw output:
`_research/parallel-ghidra-research/loc-manager-recon.txt`.

(API note for the next agent: Ghidra 12.1 removed
`DefinedDataIterator.definedStrings(Program)` — use
`currentProgram.getListing().getDefinedData(true)` + `Data.hasStringValue()`.
The pre-existing `FindPostUpdateHookCandidates.java` still fails to compile on
this same removed API; it's skipped, harmless.)

## Verified facts

### The class + its source
- RTTI type descriptor `.?AVCLocalizedStringsManager@@` @ `0x184a40e80`.
- The CryEngine source path is in the binary:
  `…\CryEngine\CrySystem\LocalizedStringManager.cpp` (so this IS CryEngine's
  `CLocalizedStringsManager`, statically linked into WHGame.dll — matches the
  "CryEngine is static-linked" finding).
- Constructor: **`FUN_1809f0ce4`** — `*this = CLocalizedStringsManager::vftable`
  (Ghidra resolved the vftable symbol). Multi-inheritance: `this[0]` and
  `this[1]` both take vftables (an `ILocalizationManager` base + a second base).
- `LocalizeString` (`@key` substitution resolver): **`FUN_18051d534`**.
- `AddLocalizedString` (interns a key with its int-ID): **`FUN_180eff9f4`**.

### The key→int-ID mechanism — THE INT-ID IS A VECTOR INDEX (verified)
From `AddLocalizedString` (`FUN_180eff9f4`), decompiled:
```c
puVar1 = (undefined8 *)param_2[10];          // vector end ptr
if (puVar1 == (undefined8 *)param_2[0xb]) {  // == capacity → grow
    FUN_1803b2b4c(param_2 + 9, puVar1, &entry);   // push_back (realloc path)
} else {
    *puVar1 = entry;  param_2[10] += 8;            // push_back (fast path)
}
plVar5 = FUN_1809f1c70(param_2 + 1, local_28, key);  // map<key,entry*> insert
*(plVar5 + 0x18) = entry;                            // store entry ptr in node
// logged ID = (vector_end - vector_begin)/8 - 1  == index of just-added entry
"<Localization> Add new string <%u> with ID %d to <%s>"
```

Each per-language string-table object (`param_2`) holds **two parallel
containers**:
- **`vector<entry*>`** at fields `[9]` (begin), `[10]` (end), `[0xb]`
  (capacity). **The int-ID == the entry's index in this vector** (the logged
  `ID %d` is literally `(end-begin)/8 - 1`). 8-byte stride (pointer entries).
- **`map<key_string, entry*>`** rooted at `param_2 + 1` (RB-tree; the
  constructor `FUN_1809f0ce4` inits the sentinel node:
  `node->{next,prev,parent}=node; *(u16*)(node+0x18)=0x101`). Entry ptr stored
  at node `+0x18`.

So: **key → map lookup → entry; int-ID → vector[id] → entry.** They share the
same entry objects. The entry struct's text/key fields are at offsets not yet
fully mapped (the lookup vtable method below resolves text from an entry).

### The lookup vtable slot (from LocalizeString)
`LocalizeString` (`FUN_18051d534`) resolves a `@key` by calling, on the manager:
```c
(**(code **)(*param_1 + 0xb8))(param_1, key_substr, out_buf, 0);   // key → text
```
**vtable offset `0xB8` = slot 23** (0xB8/8) is the **key-string → text** lookup.
`param_1[3]` (offset 0x18) is the current-language pointer ("No language set"
guard). NB this slot takes the KEY STRING, not an int-ID — see the sub-risk.

## Bearing on the sub-feature's open sub-risk (important)

The §6 sub-risk was: do consumers reference the int-ID as an immediate operand
(statically findable) or via dynamic lookup? This RE surfaces a SHARPER point:

- The int-ID is a **per-table vector index assigned at load order** — it is
  **runtime-determined and load-order-dependent**, exactly why it must be
  dumped live (confirms the runtime-probe approach).
- BUT the primary engine lookup path seen here (`LocalizeString` → vtable[23])
  is **key-STRING based**, not int-ID based. If most consumers call the
  string-keyed path, the "consumer references an int-ID" model may apply to
  FEWER call sites than assumed — many consumers may pass the `@key` string
  (which IS often a literal, or built from literals), reachable by the EXISTING
  string-anchor + call-graph mechanism without needing the int-ID at all.
- **Re-assess before building the dump probe:** confirm how many consumers use
  an int-ID GetString-by-index path vs the string-keyed vtable[23] path. If the
  string path dominates, the int-ID runtime dump may be lower-value than the §6
  plan assumed. This is checkable (find callers of the by-index getter) and
  should be probed before §6 step 3 — do not assume (AP10).

## Runtime-dump implications (for §6 step 3, when built)

To dump key→int-ID live, the probe walks, per loaded language table:
1. reach the manager: `gEnv` (muyuanjin recipe) → `pSystem` → the
   localization-manager pointer (the `ISystem` slot returning it is not yet
   pinned — find `GetLocalizationManager`/equivalent, OR hook the ctor
   `FUN_1809f0ce4` to capture `this`).
2. for each entry index `i` in the `vector<entry*>` (fields [9]/[10]): the
   int-ID is `i`; read the entry's key string (offset TBD) — or walk the
   `map<key,entry*>` and cross-reference vector position for the ID.

Two fields still unpinned (not blocking the decision, needed for the probe):
the entry struct's key+text offsets, and the `ISystem`-slot that returns the
manager (ctor-hook sidesteps this).

## Deliverable summary (verified facts, ready for seed provenance)

| Fact | Value | Tier/evidence |
|---|---|---|
| `CLocalizedStringsManager` ctor | `FUN_1809f0ce4` (RVA 0x9f0ce4) | Ghidra; sets `CLocalizedStringsManager::vftable` |
| `LocalizeString` | `FUN_18051d534` (RVA 0x51d534) | Ghidra; refs "LocalizeString: No language set." |
| `AddLocalizedString` | `FUN_180eff9f4` (RVA 0xeff9f4) | Ghidra; "Add new string <%u> with ID %d" |
| int-ID definition | per-language `vector<entry*>` index (8-byte stride, fields [9]/[10]/[0xb]) | `AddLocalizedString` decompile |
| key→entry map | `map<key,entry*>` rooted at `this`-table `+0x8`, entry ptr at node `+0x18` | ctor + AddLocalizedString |
| key→text lookup | manager vtable slot 23 (offset 0xB8), `(this, key, out, 0) → bool` | LocalizeString decompile |
| current-language field | manager offset 0x18 (`this[3]`) | LocalizeString "No language set" guard |

None of these are in the Address Library yet; recording is a separate
`/execute` step (not done here — this skill verifies, the caller records).

## Coverage measurement (2026-05-26) — string path vs int-ID

Script: `measure_loc_coverage.py` (here) → `loc-coverage-result.txt`. Cross-refs
the English-pak loc keys against WHGame.dll string literals.

| Population | Keys | Binary-literal (string-path reachable) | int-ID-only |
|---|---|---|---|
| **All loc keys** | 203,807 | 292 (0.1%) | 99.9% |
| **Gameplay/UI** (menus/items/hud/misc/tutorials/ingame) | 13,768 | 289 (**2.1%**) | **97.9%** |
| **Content** (dialog/quest/soul) | 190,039 | 3 (0.0%) | 100% |

**Two decisions this drives:**

1. **TWO-table loc partition (user decision).** 93% of loc text is dialogue/quest
   CONTENT with no gameplay function behind it (spoken lines piped to a subtitle
   renderer). Split into:
   - **`loc_gameplay`** (~13.7K UI/HUD/menu/item keys) — feeds `find{text=}`
     function discovery.
   - **`loc_content`** (~190K dialogue/quest/soul keys) — separate table,
     text-queryable (find/retext a line), NOT walked for function discovery.
   Classifier = source XML file (`text_ui_dialog/quest/soul.xml` = content; the
   rest = gameplay).

2. **The int-ID path IS required for gameplay text.** Even against the gameplay
   denominator, the string path reaches only 2.1% — so 98% of UI text needs the
   int-ID mechanism, not string literals. The "maybe int-ID is an edge case" hope
   is dead.

## THE remaining gating unknown — does int-ID link to consuming functions?

The int-ID runtime dump yields `key → int-ID`. That only helps `find{text=}` if
the int-ID statically links to the CONSUMING gameplay function. The RE found the
engine's own lookup is STRING-keyed (vtable[23] takes the key string), so it is
NOT yet established that gameplay consumers reference the int-ID in a
findable way. **Probe this BEFORE building the runtime dump** (next step): do
consuming functions take an int-ID (immediate operand / GetString-by-index call)
or the key string? If string-keyed end-to-end, the int-ID dump won't bridge to
functions and a different mechanism is needed. Checkable statically; do not
assume (AP10).

## Linkage probe result (2026-05-26) — Outcome C (int-ID getters EXIST, but the bridge is non-trivial)

Resolved the `CLocalizedStringsManager` vtable @ `0x183dbcf90` (42 methods) from
the ctor's LEA (`DumpLocVtable.java` → `loc-vtable-recon.txt`). The getter
surface has BOTH int-ID and key-string accessors:

- **By-INT-ID getters EXIST** (so int-ID CAN link to consumers in principle):
  - slot 1 `char* FUN_1804d99e0(this, uint id)` — get-string-by-ID, returns text.
  - slot 27 `FUN_18242cfd0(this, int idx, …)`, slot 28 `FUN_18242cf10(this, int idx, …)`.
  - slots 35/37 `(this, int, …)` — more int-indexed accessors. ~21/42 methods
    take an int/uint param.
- **By-KEY (string) getters EXIST**: slot 23 `FUN_18051d6c8(this, char* key, …)`
  (the LocalizeString path), slots 17/29/30 `char*`. ~5/42 take `char*`.

**Two caveats that leave the int-ID→function bridge UNPROVEN (not a quick fix):**
1. **Virtual dispatch.** Getters are reached as `this->vtable[slot](...)`, not
   direct calls (direct xrefs ≈ 2 = the vtable slot). "Find callers of the by-ID
   getter" is therefore not a simple xref scan — it needs virtual-callsite
   analysis (`call [reg+0xD8]` on a loc-manager `this`). Same shape as the
   original sub-risk, one level up.
2. **Operand constancy unverified.** Even at a by-ID call site, whether `int id`
   is a findable CONSTANT vs runtime-computed is NOT checked. If computed, the
   static ID→fn link fails even with the dump.

**Verdict:** the int-ID path is REACHABLE (getters exist), but the static
ID→function bridge AFTER a runtime dump is non-trivial and unproven — it needs
virtual-callsite resolution + operand-constancy confirmation, neither done. The
build/defer decision must weigh that the bridge is itself unproven RE, not a
given. This exceeded a quick probe; stopping here per skill scope (research
verifies; the build/defer call + any deeper bridge RE is the caller's).

## Bridge constancy probe (2026-05-26) — INCONCLUSIVE (probe flaw, recorded honestly)

User chose to build the FULL chain → next static step was to prove the
ID→function bridge: are by-ID-getter call sites passing a CONSTANT id (static
link works) or a computed id (it doesn't)?

`ProbeLocIdCallers.java` scanned for indirect `call [reg+disp]` at the by-ID
getter offsets {0x8, 0xD8, 0xE0} and checked whether the id arg was a const
immediate. Raw result: 13,906 sites, 503 const (3.6%), 13,403 computed/unknown.

**This result is INVALID — do not act on it.** Two flaws:
1. **The 0x8 offset over-matches catastrophically.** `call [reg+0x8]` is vtable
   slot 1 on ANY C++ object, not just the loc manager. The vast majority of the
   13,906 are unrelated virtual calls on other classes. The probe never
   constrained `reg` to a loc-manager `this`, so it counted the whole binary's
   slot-1 virtual calls.
2. **"computed/unknown" conflates genuine-computed with scan-missed** (the
   12-instruction backward MOV-EDX/R8D scan simply not finding the load).

So the const-vs-computed split for LOC-getter calls specifically is still
UNKNOWN. The probe needs a redesign that (a) constrains the receiver to a
loc-manager instance (taint/track the `this` from a known manager source, or
restrict to the 0xD8/0xE0 offsets which are far less generic than 0x8 and
re-examine only those), and (b) distinguishes computed from scan-missed.

Per AP10: a probe whose result is an artifact of the probe's own flaw is not
evidence. Recording the flaw, not the number. The bridge-constancy question is
deferred to a redesigned probe (or to the runtime-dump phase, which can capture
call-site→id pairs LIVE — sidestepping the static-constancy question entirely:
if static linkage is unreliable, the live dump records which call site requested
which id as the game runs).

## Bridge probe, 2nd run + conclusion (2026-05-26) — static bridge is hard; LIVE capture is the answer

Re-ran restricted to 0xD8/0xE0 (dropped the generic 0x8): 1,727 sites, 69 const
(4%), 1,658 computed. Still confounded — 0xD8/0xE0 are slots 27/28 on ANY class
with a big-enough vtable, not uniquely the loc manager; isolating loc-manager
receivers needs `this`-taint dataflow (a substantial analysis, not a script).
But the consistent signal across BOTH runs: **the id arg is COMPUTED (loaded
from a field/struct), not an immediate, at the overwhelming majority of sites.**

**Conclusion (stop iterating static probes — AP10):** a purely-static
ID→function bridge is unreliable here — virtual dispatch hides which call site
is on a loc manager, and IDs are computed at runtime in normal C++ fashion. Do
NOT keep hopping static-probe designs.

**The runtime dump IS the bridge — capture it live.** As the game runs, real
call sites invoke the by-id getters with real ids. A runtime probe that records,
per call, the tuple **(caller return-address → function, id, resolved key/text)**
observes the entire bridge directly — no static constancy needed. So the
runtime dump's job widens from "key→id" to **"caller-function ↔ id ↔ key"**,
which is exactly what `find{text=}` needs: text → key → id → the functions
observed requesting it. This sidesteps the unsolved static problem entirely.

**Implication for the build (/execute):** the runtime dump probe should hook the
by-id getter(s) (slots 1/27/28 on the loc-manager vtable @ 0x183dbcf90) and log
the caller return address + id per call, accumulated across a play session.
Coverage becomes a function of what the player exercises in-game — which fits
the "find what they need" goal (the text a player actually sees is the text that
got requested, hence captured). Static enumeration of the key→id table (from the
vector<entry*>) still gives the full key↔id map; the live hook adds the id→caller
edges.
