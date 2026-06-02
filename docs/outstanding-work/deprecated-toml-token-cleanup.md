# Deprecated [[...]] TOML-token Cleanup — fix-list

Status: AWAITING USER APPROVAL. This is a catalogue + plan only — no file is
edited by writing this doc. The rewrite executes only after sign-off.

## Summary

- **Total Category-C occurrences classified: 209** — **70 FIX**, **139 KEEP**.
  (FIX count is occurrence-level; several share a line, so the FIX rows resolve
  to ~58 distinct line edits across 31 files.)
- **What's deprecated:** the `[[...]]` TOML *behavior* primitives removed in the
  Phase-5 schema cut (`95854fe`) — `[[patch]]`, `[[hook]]`, `[[mid_hook]]`,
  `[[trampoline]]`, `[[scan]]`, plus the never-built placeholders `[[command]]`,
  `[[event]]`, `[[vtable_hook]]`, `[[address]]`, `[[inject]]`, `[[call_redirect]]`,
  `[[intercept]]`, `[[replace]]`, `[[constant]]`. **What replaced them:** behavior
  now ships in code — Lua `kcdx.bytes` / `kcdx.hook` (incl. `mode=mid`) /
  `kcdx.code` / `kcdx.command` / `kcdx.on` / `kcdx.publish` / `kcdx.scan` in
  `plugin.lua`, or the C++ interfaces (`kcdxBytesInterface` /
  `kcdxHookInterface` / `kcdxTrampolineInterface`). `kcdx.toml` is now
  manifest-only (`[kcdx]` / `[plugin]` / `[entrypoints]` / `[load_order]`).
  **Excluded from scope (not Category-C):** C++ attributes (`[[nodiscard]]`,
  `[[maybe_unused]]`, etc.) and live TOML array-tables that still exist
  (`[[plugin]]` in `load_order.toml`, author-target tables, `[[target]]`).
- **Scope decision:** fix **PRESCRIPTIVE survivors only** — text that frames a
  deleted primitive as a current authoring path, live schema, or
  thing-you-can-do-now. **KEEP** every historical/exempt occurrence: migration
  docs, superseded design sections, restructure/phase planning, research,
  examples/archive, known-issues trails, and comparative teaching framing
  ("succeeds / replaces the v0.1 X"). This mirrors `deletion-hygiene.md`'s
  prescriptive-vs-historical split.

---

## Fixes to apply (grouped by file)

The `proposed replacement` is the exact reworded line/comment to write.
Files are ordered: governance (.claude/, CLAUDE.md) → public author docs →
engine headers/include comments → engine .cpp comments → test-plugins → vendor.

### Layer 1 — Governance rules (.claude/) — always-loaded, private

#### `.claude/rules/hook-engine.md`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 25 | `[[vtable_hook]]` | `When adding a new engine (e.g. future ` + "`[[vtable_hook]]`" + `), register footprints with conflict_engine.` | `When adding a new engine (e.g. future vtable-hooking primitive), register footprints with conflict_engine.` | no |

#### `.claude/rules/anti-patterns.md`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 61 | `[[vtable_hook]]` | `New engine (e.g. ` + "`[[vtable_hook]]`" + `) → extend ` + "`WriteKind`/`Category`" + `, register footprints, add no cross-engine knowledge.` | `New engine (e.g. a future vtable-hooking primitive) → extend ` + "`WriteKind`/`Category`" + `, register footprints, add no cross-engine knowledge.` | no |

#### `.claude/rules/lua-callback-threading.md`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 34 | `[[vtable_hook]]` | `Every new dispatcher (` + "`hook_chain`" + `, future ` + "`[[vtable_hook]]`" + `, etc.) goes through the same thread-check helper.` | `Every new dispatcher (` + "`hook_chain`" + `, future vtable-hooking primitive, etc.) goes through the same thread-check helper.` | no |

Note: `CLAUDE.md:91` and `.claude/rules/address-library.md:36` also contain
`[[vtable_hook]]` but are KEEP (historical-comment framing — see KEEP table).

### Layer 2 — Public author docs (`docs/`)

#### `docs/cpp/hook.md`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 196 | `[[address]]` | `via ` + "`[[address]]`" + ` / ` + "`kcdx.address`" + ` or a cross-plugin export), and refer to it` | `via ` + "`kcdx.address`" + ` or a cross-plugin export), and refer to it` | yes |

### Layer 3 — Engine headers + include comments (public-facing)

#### `include/kcdx/Interfaces.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 191 | `[[patch]]` / `[[hook]]` | `// one [[patch]], [[hook]], or kcdx.hook entry that overlaps a queried` | `// one byte-patch entry (kcdx.bytes), hook entry (kcdx.hook), or higher-level` | yes |
| 196 | `[[mid_hook]]` | `// Scope note: [[mid_hook]] / kcdx.hook mode=mid conflicts are NOT reported` | `// Scope note: mid-function hook conflicts (kcdx.hook mode=mid) are NOT reported` | yes |
| 248 | `[[trampoline]]` / `[[hook]]` | `// ` + "`[[trampoline]]`" + ` / ` + "`[[hook]]`" + ` TOML entries with an ` + "`export = \"...\"`" + ` field` | `// code exports (kcdx.code{export=...}) and hook entries (kcdx.hook{export=...}) with an ` + "`export = \"...\"`" + ` field` | yes |
| 331 | `[[patch]]` | `// For [[patch]] entries the "target" matches if the patch's write` | `// For byte-patch entries (kcdx.bytes) the "target" matches if the patch's write` | yes |
| 335 | `[[hook]]` | `// For [[hook]] entries the "target" matches if the hook's resolved` | `// For hook entries (kcdx.hook) the "target" matches if the hook's resolved` | yes |
| 341 | `[[mid_hook]]` | `// losers (applied == 0). [[mid_hook]] / kcdx.hook mode=mid conflicts` | `// losers (applied == 0). Mid-function hook conflicts (kcdx.hook mode=mid)` | yes |
| 382 | `[[trampoline]]` | `// name resolves to your own kcdx.code{export=} / [[trampoline]] export` | `// name resolves to your own kcdx.code{export=} / kcdx.hook{export=} symbols` | yes |
| 1209 | `[[patch]]` + `[[hook]]` | `// matches the [[patch]] / [[hook]] schema exactly.` | `// matches the kcdx.bytes / kcdx.hook Lua schema exactly.` | yes |
| 1240 | `[[patch]]` | `// documented contract (matches mempatch's [[patch]] same-length rule).` | `// documented contract (matches the same-length rule for byte patches).` | yes |
| 1649 | `[[address]]` | `// (publishes via [[address]] / kcdx.address or via a cross-plugin export),` | `// (publishes via kcdx.address or via a cross-plugin export),` | yes |

Note line 1240: drop `mempatch's` too — mempatch is deprecated and naming it
here is stale. Replacement above already omits it.

#### `src/conflict_engine.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 10 | `[[patch]]` + `[[hook]]` + `[[mid_hook]]` | `// detection across [[patch]], [[hook]], and (future) [[mid_hook]].` | `// detection across byte patches, hooks, and mid-function hooks (kcdx.bytes, kcdx.hook, kcdx.hook mode=mid).` | yes |
| 15 | `[[patch]]` + `[[hook]]` | `//   1. Cross-engine collisions (a [[patch]] overlapping a [[hook]] prologue)` | `//   1. Cross-engine collisions (a byte patch overlapping a hook prologue)` | yes |
| 18 | `[[mid_hook]]` | `//   3. Future engines ([[mid_hook]] etc.) would each have to know about` | `//   3. Future engines (e.g. mid-function hooks) would each have to know about` | yes |
| 53 | `[[patch]]` | `    Patch,         // [[patch]] same-length byte rewrite` | `    Patch,         // byte patch (kcdx.bytes): same-length byte rewrite` | yes |
| 54 | `[[hook]]` | `    HookPrologue,  // 5-byte rel32 jmp MinHook installs at a [[hook]] target` | `    HookPrologue,  // 5-byte rel32 jmp MinHook installs at a hook target (kcdx.hook)` | yes |
| 78 | `[[patch]]` | `// [[patch]] produces these (its ` + "`original`" + ` field). Hooks don't verify; they` | `// Byte patches (kcdx.bytes) produce these (its ` + "`original`" + ` field). Hooks don't verify; they` | yes |
| 107 | `[[hook]]` | `    // Two [[hook]]s target the same function entry. First-wins in v0.1` | `    // Two hooks target the same function entry. In load order, the first applies` | yes |
| 112 | `[[hook]]` + `[[patch]]` | `    // [[hook]]'s 5-byte rel32-jmp footprint overlaps an EARLIER [[patch]]'s` | `    // A hook's 5-byte rel32-jmp footprint overlaps an EARLIER byte patch's` | yes |
| 118 | `[[patch]]` + `[[hook]]` | `    // [[patch]] write range overlaps an EARLIER [[hook]]'s 5-byte rel32-jmp` | `    // A byte patch write range overlaps an EARLIER hook's 5-byte rel32-jmp` | yes |

Note line 107: replacement also rewords the stale "First-wins in v0.1" — the
`kcdx.hook` model is load-order chaining, not first-wins (per `hook-engine.md`).
Confirm the surrounding comment still reads coherently after the edit.

#### `src/patch_engine.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 44 | `[[patch]]` | `    // TOML [[patch]] rows) or by lua_bind_bytes (for kcdx.bytes calls),` | `    // byte-patch entries from kcdx.toml) or by lua_bind_bytes (for kcdx.bytes calls),` | yes |
| 74 | `[[trampoline]]` + `[[hook]]` | `    //                   (resolves to a [[trampoline]] / [[hook]] export).` | `    //                   (resolves to a kcdx.code{export=} / kcdx.hook{export=} symbol).` | yes |

#### `src/patch_engine.cpp`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 132 | `[[patch]]` | `// which means any patches kcdx has already applied (TOML [[patch]] /` | `// which means any patches kcdx has already applied (kcdx.bytes /` | yes |
| 133 | `[[hook]]` | `// [[hook]] entries that ran during first-update-tick) are visible. An` | `// kcdx.hook entries that ran during first-update-tick) are visible. An` | yes |
| 282 | `[[trampoline]]` | `    // Used when a patch wants to write into another plugin's [[trampoline]]` | `    // Used when a patch wants to write into another plugin's code region (allocated via kcdx.code)` | yes |
| 294 | `[[patch]]` | `        // (kcdx.bytes calls) or by config.cpp's LoadOneFile (TOML [[patch]]` | `        // (kcdx.bytes calls) or by config.cpp's LoadOneFile (byte-patch entries from` | yes |

#### `src/trampoline_engine.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 11 | `[[trampoline]]` | `// One [[trampoline]] entry parsed from a kcdx.toml file. Unlike [[patch]]` | `// One kcdx.code allocation (from a kcdx.toml file). Unlike byte patches` | yes |
| 12 | `[[patch]]` + `[[hook]]` | `// and [[hook]], [[trampoline]] doesn't target an address in WHGame.dll —` | `// and hooks, kcdx.code allocates fresh executable memory rather than targeting` | yes |
| 68 | `[[trampoline]]` | `// Allocate every [[trampoline]] entry's region, copy bytes in, NOP-pad the` | `// Allocate every kcdx.code allocation's region, copy bytes in, NOP-pad the` | yes |

Note lines 11–12 form a two-line sentence; the two replacements must be applied
together so the joined comment reads: "One kcdx.code allocation … Unlike byte
patches and hooks, kcdx.code allocates fresh executable memory rather than
targeting an address in WHGame.dll —". Verify continuity onto line 13.

#### `src/scan_engine.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 98 | `[[patch]]` | `// Run every loaded scan: resolve via the same locator pipeline used by` | `// Run every loaded scan: resolve via the same locator pipeline used by` (unchanged; see line 99) | yes |
| 99 | `[[patch]]` + `[[hook]]` | `// [[patch]] / [[hook]], log the outcome. Safe to call on the first` | `// byte patches and hooks, log the outcome. Safe to call on the first` | yes |

Note: lines 97–99 are one comment. The verified text on line 99 reads
`// [[patch]] / [[hook]], log the outcome. Safe to call on the first` and line 99
continues onto line 100 `// update tick (same point [[patch]] applies happen).` —
line 100's `[[patch]]` was NOT flagged in the input JSON but is the SAME
prescriptive comment; see Open Questions #3.

#### `src/scan_engine.cpp`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 5 | `[[patch]]` | `// scan path to [[patch]] (so identical match counts) but no write,` | `// scan path to byte patches (so identical match counts) but no write,` | yes |

Note: the input KEEP list also lists `src/scan_engine.cpp:5` `[[patch]]` as
`comparative` (KEEP). This is a duplicate-classification conflict — see Open
Questions #4.

#### `src/scripting.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 9 | `[[hook]]` + `[[mid_hook]]` | `// in [[hook]] / [[mid_hook]] kcdx.toml entries. Callbacks are stored as` | `// in kcdx.hook entries (including mid-function hooks). Callbacks are stored as` | yes |
| 70 | `[[hook]]` | `// Non-owning registration. The caller (lua_memory.cpp / future [[hook]]` | `// Non-owning registration. The caller (lua_memory.cpp / future kcdx.hook` | yes |
| 100 | `[[hook]]` | `// Clear all callbacks for a target (used when a [[hook]] is being` | `// Clear all callbacks for a target (used when a hook (kcdx.hook) is being` | yes |
| 133 | `[[mid_hook]]` | `// Skip-original flag for [[mid_hook]] call_original="auto" mode.` | `// Skip-original flag for mid-function hooks (kcdx.hook mode=mid) call_original="auto" mode.` | yes |

#### `src/hook_engine.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 13 | `[[hook]]` | `// One [[hook]] entry parsed from a kcdx.toml file. Locator fields reuse the` | `// One hook entry (from kcdx.hook Lua call or kcdx.toml file). Locator fields reuse the` | yes |
| 73 | `[[mid_hook]]` | `// [[mid_hook]] entries from kcdx.toml. Distinct from HookEntry` | `// Mid-function hook entries (kcdx.hook mode=mid). Distinct from HookEntry` | yes |
| 98 | `[[mid_hook]]` | `// call_original mode for [[mid_hook]] — decides whether the captured` | `// call_original mode for mid-function hooks (kcdx.hook mode=mid) — decides whether the captured` | yes |

Note line 13: "parsed from a kcdx.toml file" is itself stale (hooks no longer
parse from TOML), but the proposed replacement retains "or kcdx.toml file" from
the input JSON. See Open Questions #5 — recommend dropping the TOML clause
entirely.

#### `src/lua_registry.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 44 | `[[patch]]` | `    Bytes,        // succeeds [[patch]]` | `    Bytes,        // succeeds kcdx.bytes` | yes |
| 45 | `[[hook]]` + `[[mid_hook]]` | `    Hook,         // succeeds [[hook]] + [[mid_hook]] + dynamic_hook` | `    Hook,         // succeeds kcdx.hook, kcdx.hook mode=mid, and dynamic_hook` | yes |

Note: "succeeds X" here is comparative framing ("succeeds [[patch]]"). Per
`deletion-hygiene.md`, comparative "succeeds the v0.1 X" is HISTORICAL/exempt.
These were classified FIX in the input. See Open Questions #6.

#### `src/lua_memory.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 84 | `[[hook]]` | `// :get() / :set(newVal) the value. Used by [[hook]] lua_callback and` | `// :get() / :set(newVal) the value. Used by kcdx.hook lua_callback and` | yes |
| 85 | `[[mid_hook]]` | `// [[mid_hook]] arg marshaling.` | `// kcdx.hook mode=mid arg marshaling.` | yes |

#### `src/ldr_notify.h`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 8 | `[[patch]]` | `// LDR DLL notification — apply before_game-zone [[patch]] entries` | `// LDR DLL notification — apply before_game-zone byte-patch entries` | yes |
| 38 | `[[patch]]` | `// [[patch]] entries. Capability gating in load_order.cpp already` | `// byte-patch entries. Capability gating in load_order.cpp already` | yes |
| 45 | `[[patch]]` | `// Walk before_game-zone [[patch]] entries; apply any whose target` | `// Walk before_game-zone byte-patch entries; apply any whose target` | yes |

Note line 38: the verified comment continues "already downgrades
hook/mid_hook/trampoline plugins to after_game." Those bare words
(`hook/mid_hook/trampoline`) are not `[[...]]` tokens and were not flagged;
they read as capability-category names, leave as-is (or optionally reword to
"hook/mid-hook/trampoline plugins" for clarity — low priority).

#### `CMakeLists.txt`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 243 | `[[mid_hook]]` | `                             # [[mid_hook]] stack_restore_offset.` | `                             # mid-function hook (kcdx.hook mode=mid) stack_restore_offset calculation.` | yes |

### Layer 4 — Engine .cpp comments + author-facing error message

#### `src/config.cpp`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 313 | `[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]]` | `// ([[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]]) — previously` | `// (legacy behavior tables) — previously` | yes |
| 324 | `[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]]` | `"[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]] were "` | `"legacy behavior tables (the previous TOML schemas) were "` | yes |
| 922 | `[[patch]]/[[hook]]` | `// counter and skip everything else (no [[patch]]/[[hook]]` | `// counter and skip everything else (no legacy TOML behavior` | yes |
| 935 | `[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]]` | `// legacy [[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]]` | `// legacy behavior tables (those TOML primitives)` | yes |
| 998 | `[[patch]] / [[hook]] / [[mid_hook]] / [[trampoline]] / [[scan]]` | `// [[patch]] / [[hook]] / [[mid_hook]] / [[trampoline]] / [[scan]]` | `// legacy behavior tables` | yes |

Note line 324 is a USER-FACING error string. The full message (verified) reads:
`"unknown top-level table '[...]' (valid top-level tables: [kcdx], [plugin],
[entrypoints], [load_order]; legacy behavior tables like
[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]] were removed — behavior
ships in plugin.lua / a DLL)"`. The bracket tokens here teach the author exactly
which removed tables triggered the reject — see Open Questions #1 (this rewrite
may REDUCE author clarity; recommend keeping the bracket list in the error
string, fixing only the comments).

#### `src/dllmain.cpp`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 67 | `[[patch]]` | `    // before_game-zoned [[patch]] entries could apply against ntdll /` | `    // before_game-zoned byte-patch entries could apply against ntdll /` | yes |
| 388 | `[[patch]]` | `// resolution, applies before_game-zoned [[patch]] entries to any` | `// resolution, applies before_game-zoned byte-patch entries to any` | yes |
| 440 | `[[patch]]` | `    // Apply before_game [[patch]] entries against modules already mapped` | `    // Apply before_game byte-patch entries against modules already mapped` | yes |

### Layer 5 — Test-plugins

#### `test-plugins/cap-49-fix-stray-table/kcdx.toml`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 31 | `[[patch]]` | `[[patch]]` (the table header on line 31, with body on 32-34) | **DO NOT rewrite to a comment** — see Open Questions #2 | yes |

This is a REJECTION-TEST FIXTURE. The `[[patch]]` block on lines 31-34 is the
TEST VEHICLE: the manifest parser MUST reject a stray top-level table, and this
plugin exists to prove it does. Replacing the live `[[patch]]` table with a
comment **destroys the test** (nothing left to reject; `is_rejected` flips to
false; the cap-49-reject-stray-table row FAILs). The fixture's own header
comments (lines 5, 16, 25, 27) already frame `[[patch]]` correctly as "a legacy
behavior table that was removed." **Recommendation: KEEP line 31 unchanged**
(the live table is required), and add NO comment that presents it as a feature.
Surfaced as Open Questions #2 — do not edit without the user's call.

### Layer 6 — Vendor (third-party, but public-facing)

#### `vendor/asmjit/VENDORED.md`
| line | token | current text | proposed replacement | public? |
|---|---|---|---|---|
| 29 | `[[hook]]`, `[[mid_hook]]` | `trampolines used by ` + "`[[hook]] lua_callback`" + ` and ` + "`[[mid_hook]]`" + `.` | `trampolines used by the ` + "`kcdx.hook`" + ` interface with Lua callbacks and by the mid-function hook interface.` | yes |

---

## Kept as historical (no change) — by exempt category

139 occurrences preserved. Grouped by why they are exempt:

| exempt category | count | example files |
|---|---|---|
| superseded-section | 64 | `docs/design.md` (51 — entire doc banner-superseded), `docs/outstanding-work/restructure/00-original-plan.md` (13), `docs/VERIFY_PHASE4.md`, restructure phase READMEs |
| comparative | 49 | `docs/design-gaps.md` (40 — companion gaps doc), `docs/lua/bytes.md`, `docs/cpp/hook.md` (574/577), `docs/cpp/bytes.md`, `docs/load-order.md`, `src/lua_bind_code.cpp`, `src/lua_bind_scan.cpp`, `src/lua_bind_hook.cpp`, `src/hooks.cpp`, `src/hook_chain.cpp`, test-plugins (`cap-01-patch`, `cap-03-hook-lua-callback`, `cap-20`/`21`/`22`/`32`/`33`/`51`, `comp-02`/`03`), `docs/outstanding-work/before-game-hooks.md`, `docs/outstanding-work/lua-conflict-report-mirror.md` |
| historical-comment | 13 | `CLAUDE.md:91`, `.claude/rules/address-library.md:36`, `docs/lua/scan.md`, `docs/load-order.md:283`, `src/scan_engine.h:3,15`, `src/scan_engine.cpp:144,170`, `src/modification_inventory.h:13`, `docs/re-reference/finding-patch-sites.md:149`, `docs/phase5-rom-port-plan.md`, `docs/phase5c7b-plan.md` |
| examples-archive | 5 | `docs/archive/VERIFY_PHASE1.md` (3), `examples/archive/v0.1-schema/README.md` |
| migration-doc | 1 | `docs/migration.md` |
| re-reference (research/methodology) | 2 | `docs/re-reference/writing-safe-patches.md:6`, `docs/re-reference/finding-patch-sites.md:7` (comparative-by-location) |

Plus the documented duplicate-listed `src/scan_engine.cpp:5` (`[[patch]]`,
comparative) which also appears in the FIX list — flagged below.

**Why these stay:** `deletion-hygiene.md` §"Exempt locations" makes
`docs/design.md` superseded sections, `**/migration*.md`, `**/known-issues/**`,
`**/closed/**`, `**/archive*/**`, and comparative teaching framing historical by
construction. `docs/design-gaps.md` is banner-framed as "what the spec doesn't
yet acknowledge," and its CLOSED gaps carry comparative preambles. Test-plugin
`comparative` rows reference the deleted token to explain what the plugin's
current `kcdx.*` code supersedes — past-tense by framing.

---

## Public/private boundary notes

Every FIX in an allowlisted public-facing path (`docs/`, `include/`, `src/`,
`vendor/`, `test-plugins/`, `CMakeLists.txt`) was checked against
`public-private-boundary.md`. Findings:

- **All proposed replacements are clean.** None introduces a `.claude/` path, a
  `CLAUDE.md` reference, an `_research/` citation, a bare `AP<n>`, a dev-phase
  token (`Phase 5`, `v0.2`, `FIX C`, `PROBE Q`), or the words
  Claude/Anthropic/subagent/orchestrator. They state the technical fact
  (kcdx.bytes / kcdx.hook / mid-function hook / byte patch) with no private
  citation.
- **`src/conflict_engine.h:107`** — the current text says "First-wins in v0.1."
  `v0.1` is a dev-phase/version token. The proposed replacement drops it
  entirely ("In load order, the first applies"), which is both more accurate and
  removes the version token. Good either way; the rewrite is the cleaner result.
- **`src/config.cpp:324`** (user-facing error) — proposed replacement
  "legacy behavior tables (the previous TOML schemas) were" introduces no
  private reference. Clean. (Separate UX concern in Open Questions #1.)
- **The 3 governance-rule fixes (Layer 1)** are in PRIVATE files (`.claude/`) —
  no boundary constraint applies; they may say anything. Reworded only for
  accuracy (the never-built `[[vtable_hook]]` placeholder → "vtable-hooking
  primitive").
- **No replacement still needs a boundary re-check** beyond the standard
  author-time `guard-public-private-refs.py` warn pass when each edit is
  written.

---

## Execution plan

Mechanical-rewrite work — **comments + docs only, no behavior change**. The
`.cpp`/`.h` edits touch compiling translation units but change only comment
text, so the build must stay green (a `pwsh ./build.ps1` at the end confirms no
stray edit broke a TU). Apply by layer, each layer a coherent commit:

1. **Commit 1 — governance (always-loaded first).** `.claude/rules/hook-engine.md`,
   `anti-patterns.md`, `lua-callback-threading.md` (3 edits). These load on every
   matching session, so correcting the never-built-placeholder framing first
   stops the stale reference from re-teaching itself. No build needed (Markdown).

2. **Commit 2 — public author docs.** `docs/cpp/hook.md` (1 edit). No build.

3. **Commit 3 — engine headers + include comments.** `include/kcdx/Interfaces.h`,
   `src/conflict_engine.h`, `src/patch_engine.h`, `src/patch_engine.cpp`,
   `src/trampoline_engine.h`, `src/scan_engine.h`, `src/scan_engine.cpp`,
   `src/scripting.h`, `src/hook_engine.h`, `src/lua_registry.h`,
   `src/lua_memory.h`, `src/ldr_notify.h`, `src/dllmain.cpp`, `CMakeLists.txt`.
   Run `pwsh ./build.ps1` — comment-only, expect exit 0 + unchanged artifacts.
   (No deploy/launch needed: zero observable behavior change, so the test-suite
   matrix is not affected. Per `agent-builds-and-deploys.md` the build is the
   agent's; no user launch is owed for a comment-only diff.)

4. **Commit 4 — engine .cpp comments + error string.** `src/config.cpp` (pending
   Open Questions #1 resolution on the user-facing error string). Build green.

5. **Commit 5 — vendor.** `vendor/asmjit/VENDORED.md` (1 edit). No build.

Test-plugins layer (`cap-49-fix-stray-table/kcdx.toml:31`) is **held** pending
Open Questions #2 — it is the only edit that would change behavior (it would
break a regression fixture), so it must not land as mechanical work.

Staging discipline: `git add` by exact path per layer (shared tree — never
`git add -A`). `/commit` self-invokes at each layer boundary.

---

## Open questions for the user

Six genuinely-ambiguous calls. Do not guess — decide each:

1. **`src/config.cpp:324` user-facing error string — keep the bracket list?**
   The error message teaches the author EXACTLY which removed tables triggered
   the reject (`[[patch]]/[[hook]]/[[mid_hook]]/[[trampoline]]/[[scan]] were
   removed`). The input JSON proposes "legacy behavior tables (the previous TOML
   schemas) were removed," but that is LESS specific — an author who wrote a
   `[[patch]]` no longer sees their exact mistake named. The bracket spelling
   here is arguably *teaching*, not a prescriptive survivor. **Recommendation:
   KEEP the bracket list in the error string** (it is past-tense and names the
   author's exact mistake), fix only the comment occurrences (313/922/935/998).
   Your call: reword the string, or keep it?

2. **`test-plugins/cap-49-fix-stray-table/kcdx.toml:31` — the live `[[patch]]`
   table is the test vehicle.** This fixture exists to prove the parser REJECTS
   a stray top-level table. The `[[patch]]` block on lines 31-34 MUST stay a
   real table or the test has nothing to reject. The input's proposed
   replacement (turn it into a comment) would silently break the
   cap-49-reject-stray-table row. **Recommendation: KEEP line 31 unchanged**;
   the surrounding comments already frame it as a removed legacy table.
   Reclassify as KEEP? (I believe the FIX verdict here was a misread of a
   fixture as documentation.)

3. **`src/scan_engine.h:100` — sibling occurrence not in the FIX list.** Line
   100 (`// update tick (same point [[patch]] applies happen).`) is the SAME
   prescriptive comment as the flagged lines 98-99 but was not in the input
   JSON. Fold it into the same edit? **Recommendation: yes** — reword to
   "// update tick (same point byte-patch applies happen)." so the comment is
   internally consistent.

4. **`src/scan_engine.cpp:5` — listed in BOTH FIX and KEEP.** The input JSON has
   this line as FIX (prescriptive) AND as KEEP (comparative). It cannot be both.
   The verified text "// scan path to [[patch]] (so identical match counts) but
   no write," reads as comparative-descriptive (explaining scan reuses the patch
   locator pipeline). **Recommendation: treat as FIX** (reword to "byte patches")
   for token consistency with the rest of `scan_engine`, OR keep if you read it
   as purely comparative. Your call resolves the conflict.

5. **`src/hook_engine.h:13` and `:73` — "parsed from a kcdx.toml file" is itself
   stale.** Hooks no longer parse from TOML at all (Phase-5 cut). The input's
   line-13 replacement retains "or kcdx.toml file." **Recommendation: drop the
   TOML clause** — "One hook entry (the in-memory shape `kcdx.hook` builds)."
   Same for line 73. Keep the (stale) TOML mention, or drop it?

6. **`src/lua_registry.h:44-45` — "succeeds X" is comparative framing.**
   `deletion-hygiene.md` treats "succeeds / replaces the v0.1 X" as
   HISTORICAL/exempt. "// succeeds [[patch]]" arguably qualifies (it states the
   enum value is the successor to the deleted primitive — past-tense). The input
   classified it FIX. **Recommendation: light FIX** — keep the "succeeds"
   framing but drop the dead bracket spelling: "// succeeds kcdx.bytes". This is
   what the input proposes and it is the cleaner read. Confirm, or leave as
   historical-exempt (no change)?
