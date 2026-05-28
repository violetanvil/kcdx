# Public-scrub translation table (PRIVATE working doc)

The fixed token→replacement mapping for the public/private scrub (AP16). Applied
uniformly across all 168 public-facing files so the same concept reads the same
everywhere. This doc lives under `docs/outstanding-work/` — PRIVATE, never
published — so it may reference private tokens freely.

**Principle (locked):** preserve the knowledge, drop the private citation. Every
rewrite keeps the technical fact/reasoning and removes only the pointer to a
private rule/research file or the AI-development vocabulary.

---

## Tier 1 — AP-number parenthetical citations → drop, keep the sentence

`AP<n>` in a public file is almost always a *parenthetical annotation* on a
statement that already carries (or should carry) the fact. The rewrite drops the
`(AP<n>)` / `per AP<n>` and, where the sentence leaned on the citation to MEAN
something, inlines the one-clause concept. Never leave a dangling "per ." or "(
)".

| AP | Concept (the fact to preserve in-clause if the citation was load-bearing) |
|----|---------------------------------------------------------------------------|
| AP1  | use a resolved address-library ID, not a hardcoded RVA (RVAs shift per game update) |
| AP2  | signature verified against the binary, not inferred from prologue shape |
| AP3  | vtable slot empirically probed against the binary, not assumed from a CryEngine header |
| AP4  | installed through the conflict engine (footprint), not a direct MinHook |
| AP5  | uses the live Lua C API, not a kcdx-side static sentinel (shared-state GC safety) |
| AP6  | callback marshaled to the main thread (the live lua_State is main-thread-only) |
| AP7  | ships a permanent regression test |
| AP9  | fixed at the cause, not silenced |
| AP10 | probed against the binary, not theorized |
| AP11 | append-only — new interface members at the struct END (fixed-offset ABI) |
| AP12 | the engine resolves address AND signature from a name; the author writes no hex/ABI |
| AP13 | fixed at the source now, not deferred as a someday-maybe |
| AP14 | fails LOUD with a structured error, not a silent drop/no-op |
| AP15 | the test carries a falsifiable claim — it can actually go red |

Examples (real lines from the tree):
- ❌ `// diverging from the canonical CryEngine header per AP3` → ✅ `// vtable slot empirically probed against the binary, not assumed from a header`
- ❌ `APPEND-ONLY (AP11; new interface members go at the struct END)` → ✅ `APPEND-ONLY — new interface members go at the struct END (fixed-offset ABI)`
- ❌ `that PASS was an AP15 tautology` → ✅ `that PASS was a tautology (asserted something always true)`
- ❌ `honest signal AP14 demands: the author hears their setting...` → ✅ `the honest signal: the author hears their setting...`

## Tier 2 — `.claude/rules/*.md` doc links → inline the concept, drop the link

A markdown link into the private governance tree is a broken link on public.
Replace the `[text](.claude/rules/x.md)` with a self-contained restatement. The
15 distinct targets and their one-line public restatement:

| Private link target | Public restatement (drop the link, keep this) |
|---------------------|-----------------------------------------------|
| `lua-api-surface.md` | the authoring surface is one learnable model in two languages (Lua + C++), with mirrored `kcdx.*` naming and call-shape |
| `lua-callback-threading.md` | the engine auto-marshals an off-thread hook hit to the main thread before firing a Lua callback |
| `naming-namespaces.md` | shared names are `<author>.<plugin>`-prefixed; precedence is self > engine > other; the canonical separator is a dot |
| `hook-engine.md` | `kcdx.hook` conflicts resolve by load order through the chain engine |
| `lua-bridge.md` | kcdx and the engine share one `lua_State`; use the live Lua C API (no kcdx-side sentinels) |
| `docs-discipline.md` | the doc entry ships in the same unit of work as the capability |
| `lua-precision.md` | `LUA_NUMBER` is float — integers beyond 2^24 lose precision; pointers push as light userdata |
| `results-driven.md` | probe a checkable unknown against the binary before changing code |
| `loader-architecture.md` | the loader is an own-launcher with an A/B (early/late) context split |
| `cornerstones.md` | the engine does the heavy lifting; the author declares intent (a name resolves address AND ABI) |
| `toml-schema.md` | `kcdx.toml` is manifest-only — identity + metadata, no behavior tables |
| `pak-mods.md` | pak-mod fixtures are read-only Lua probes |
| `test-suite.md` | every feature ships a permanent suite-gated regression plugin |
| `reverse-engineering.md` | game facts are verified against the binary via the reuse-first evidence ladder |
| `anti-patterns.md` | (cite the specific concept from Tier 1, not the file) |

### Tier 2b — BARE private-rule filenames (no `.claude/rules/` path)

A reference like `(naming-namespaces.md)` or `cornerstones.md §36` names a private
governance file by basename WITHOUT the path. It is still a leak — a broken link
target on public + a build-trace — and the path-based scan misses it. Apply the
SAME Tier-2 restatement: drop the filename, inline the concept. Every private-rule
basename is in scope EXCEPT `loader-architecture.md` (collides with the public
`docs/loader-architecture.md` — a bare ref there is a valid public cross-link).

## Tier 3 — provenance + misc pointers → restate without the path

| Private token | Public replacement |
|---------------|--------------------|
| `Verified by Ghidra <date>, _research/<phase>/<file>` | `verified via Ghidra analysis against the binary` (drop date+path; keep "verified vs binary") |
| `See _research/<phase>/<file>` (provenance) | drop the pointer; if it carried a fact, inline the fact (e.g. "confirmed by a body-wide stack-arg walk") |
| `_research/predecessor-sigs/...` | `a predecessor KCD project` (no path) |
| `[CLAUDE.md](../CLAUDE.md)` | restate the referenced fact inline; drop the link |
| `.claude/plans/` | drop the sentence (internal planning location — irrelevant to a public reader) |
| `CLAUDE.md` (prose mention) | "the project's contributor guide" or drop, per context |
| third-party-ghidra/ path | "the project's Ghidra analysis" (no path) |

## Tier 4 — internal dev-process vocabulary → strip the scheme, keep the fact

Beyond private paths and AP-numbers, the internal development-process naming
schemes are themselves AI-build traces. A public reader must not see that the
project was built in named phases with lettered probes. Strip the scheme token;
restate the technical fact it annotated.

| Token family | What it is | Public rewrite |
|--------------|-----------|----------------|
| `PROBE <X>` / `PROBE U.6` / `PROBE A` | the RE probe-naming scheme | drop the label; keep the finding — "live-confirmed against the binary", "verified by a runtime probe", or just state the fact. `(PROBE S/T, answered <date>)` → drop entirely. |
| `Phase <N>` / `Phase 5c.7b` / `sub-N` | the internal dev-phase scheme | drop the phase/sub reference; restate as plain present/past tense. "removed in Phase 5" → "removed (behavior ships in plugin code)". "Phase 2b sub-4: execute each…" → "execute each…". "LIVE-PENDING (Phase 2b sub-9)" → "LIVE-PENDING" or drop. |
| `FIX A` / `FIX B` / `FIX C` | the internal fix-naming scheme | drop the label; name the fix by what it does. |
| bare provenance dates (`2026-05-26`, `2026-05-22 boots`) | when/how-built timestamps | drop the date; keep "verified against the binary" / "live-confirmed". A date that is genuine product/version metadata (a game version, a changelog) stays. |
| `LIVE-PENDING` / `LIVE-CONFIRMED` | internal test-status shorthand | keep if it reads as plain status; otherwise "pending live verification" / "verified live". |
| commit SHAs as provenance (`kcdx@03e6bd0`, `commit 928fa61`) | internal build pointers | drop the SHA; keep the fact. |

Engine-vs-dev disambiguation: the GAME's own internal sequence ("the engine's
mod-load Phase 1/Phase 2") is a real technical noun — reword to "Stage 1/Stage 2"
(preserves meaning, clears the `Phase` token) rather than dropping it. Only the
DEV phases ("Phase 5c.7b", "sub-9") are the build-trace to strip.

## Out of scope for the table (handled per-site, flagged for the batch reviewer)

- `data/seeds/address_names_seed.csv` — the `notes` column has dense provenance
  prose (`kcdx-phase7-probe@<sha>`, "per AP3", `_research/...`). Dedicated pass.
  **Decided voice:** each notes row KEEPS the technical content (signature, vtable
  slot, calling convention, what the function does) plus a plain "verified against
  the binary"; it DROPS probe SHAs, AP-citations, `_research/` paths, and Ghidra
  dates. Also scrub the `source`/provenance column tokens (e.g. `kcdx-phase7-probe@<sha>`
  → a neutral `verified` marker) — that column is published too.
- Any AP-number used in a way the Tier-1 concept does not fit → flag, don't
  force the mapping.
