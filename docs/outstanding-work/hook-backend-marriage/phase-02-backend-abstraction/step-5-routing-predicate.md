# Step 5 — install-context routing predicate

**What.** Make the backend selection automatic and correct-by-construction: a
routing predicate reads the install context and picks MinHook for the loader-lock
(`early_hook`) + bootstrap-pump (`HookedUpdate`) paths and safetyhook for
everything else. This is the design's ONE safety-critical mechanism — a
loader-lock install misrouted to safetyhook is a deadlock (safetyhook's
unconditional thread-suspend under the loader lock). Covers E5, E18; resolves U5
(`../context.md`).

**Scope (commit-grain).**
- Implement the routing predicate (design §4.2): the §4.2 routing table is the
  settled OUTCOME —
  - `early_hook` (loader-lock / during DllMain / LDR callback) → MinHook
  - `HookedUpdate` bootstrap pump → MinHook
  - the frealloc canary (bootstrap-timing) → MinHook
  - `hook_chain` function-entry + mid → safetyhook
- **The mechanism resolves U5** — HOW the context is read. Pick the form that
  makes a loader-lock-install misroute IMPOSSIBLE, not merely unlikely (the design
  bar): the strongest is an explicit engine-internal backend argument at each
  install site (compile-time obvious, no runtime predicate to get wrong) OR a
  context flag whose loader-lock case is assert-guarded. Surface the chosen
  mechanism in the commit body; if the only safe form is a genuine design choice
  between two safe mechanisms, that is settled HERE by the impossible-misroute bar,
  not a user fork (the OUTCOME table is already the user's settled decision).
- `early_hook` keeps calling MinHook (its body is unchanged, design §8) — the
  predicate formalizes that it MUST, rather than it happening to. The update pump
  + frealloc canary likewise.
- A guard/assert: an install on a loader-lock context that somehow selects
  safetyhook fails loud at the selection point (`logging.md` — never a silent
  wrong-backend), so a future new install path can't silently misroute.

**Test bar.** A regression + a loader-lock-safety proof:
- The FULL cap-NN suite passes live (function-entry on safetyhook, the bootstrap
  paths on MinHook) — matrix `X/Y passing` whole.
- **Loader-lock safety (E18):** early_hook installs under the loader lock with
  ZERO deadlock and ZERO hang across repeated launches INCLUDING under
  multitasking load (the KI-0003 contention scenario); the early_hook
  MiniDmpSender ctor target still fires + logs. A FALSIFIABLE claim: a boot hang
  or a missing ctor-fire line is a FAIL. (A regression test asserting the
  predicate selects MinHook for a loader-lock-flagged install — runnable as a
  unit-level check at this step — plus the live boot proof.)
- Agent builds + deploys + enables dev mode, user launches, agent reads the log.

**Dependencies.** Step 4 (`SafetyhookBackend` must exist to route TO; the
predicate selects between two live backends). Step 3 (the interface).

**Design authority.** [`hook-backend-marriage.md §4.2, §7, §8`](../../../design/hook-backend-marriage.md)
+ US-5 — the routing table + the loader-lock constraint + the
impossible-misroute bar are built to the design.

**Disassembler-test / author-burden note.** None — routing is engine-internal,
no author knob (design §4.2: cornerstone #1, the engine does the heavy lifting; a
plugin author always goes through `hook_chain` and never names a backend).

**Reference.** [`../context.md`](../context.md) E5/E18 + U5 + the "MinHook
permanent" invariant.
