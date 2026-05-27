# Smart conflict detection for hook chaining (footprint-based coexistence)

> **Spec status: AUTHORITATIVE FUTURE-WORK SPEC.** This is not a sketch.
> It is the contract the future implementation must follow exactly. The
> sub-4 hook_chain architecture was deliberately built footprint-READY
> to make this an additive bolt-on, not a rewrite. If you are
> implementing this, every "MUST" below is load-bearing — deviating
> reintroduces the rebuild this design exists to avoid. When in doubt,
> stop and ask rather than guess; the user has flagged this as pivotal.

## 1. What ships today (v1 baseline — sub-4)

`kcdx.hook` installs through `src/hook_chain.{h,cpp}` (NOT the legacy
`hook_engine.cpp` / `conflict_engine.cpp` path — that legacy path is
reference-only and slated for removal). The model:

- **One target VA → one MinHook detour → one ordered chain of N
  callbacks.** The chain is a `std::vector<ChainEntry>` (see §4),
  ordered by unified load order `(plugin_priority asc, name asc)`.
- **Compatible hooks chain.** Multiple `before`, multiple `after`,
  `before`+`after`, `before`+`around`, etc. all coexist on one target
  and fire in load order. This is the "plugin A hooks; plugin B
  piggybacks off the same target" capability — it works in v1.
- **Incompatible hooks fail by load order (safe-but-blunt).** When a
  later hook cannot coexist, its `Add()` returns `ok=false` with a
  loud reason; the registry marks that handle `Failed`. Earlier-in-
  load-order always wins. v1's incompatibility cases are:
  1. **Signature mismatch.** The FIRST hook on a target fixes the
     thunk's marshaling signature. A later hook whose signature is
     not byte-compatible is rejected.
  2. **replace + anything, or replace + replace.** Because v1 assumes
     a `replace` touches EVERYTHING (worst-case footprint), any second
     hook on a target that already has a `replace` — or a `replace`
     landing on a target that already has any hook — is rejected.

The v1 rule is **correct, just conservative**: it never produces wrong
behavior, it only refuses combinations that MIGHT be fine. This spec is
how we make those refusals precise instead of blanket.

## 2. The capability this unlocks (the revisit trigger)

Build this when a **real total-conversion (TC) mod author** hits the
blunt rule and files a request — NOT speculatively. "TC" = a mod large
enough to replace most of the game (Fallout-London / Enderal class);
TC viability is the project's capability bar. The canonical trigger:

> "My mod and ModernRPG both want to override `IsInCombat`. Mine
> returns based on player state; theirs based on quest state. Right now
> only one loads because kcdx fails the second."

More generally — the user's worked example, which this spec MUST
support:

> Plugin A: references arg slots x1,x2,x3; **replaces** (writes) x4,x5.
> Plugin B: references arg slots x1,x2,x3,x4; **replaces** (writes)
> y1,y2.
>
> These DO NOT conflict even though A "replaces x4" and B "references
> x4" — B reads x4, A writes x4, and as long as the load order makes
> that read-after-write (or write-after-read) ordering well-defined,
> both run. The engine must be intelligent enough to see the footprints
> are compatible and chain both rather than failing B.

## 3. The footprint model (author-facing surface)

Each `kcdx.hook` registration MAY declare what it touches. Two new
optional fields in the opts table:

```lua
kcdx.hook(kcdx.addr.IsInCombat, {
    mode      = "replace",
    signature = "bool (ptr self)",
    reads     = { "self" },          -- slots/return this callback INSPECTS
    writes    = { "return" },        -- slots/return this callback MUTATES
    callback  = function(args) return true end,
})
```

### 3.1 Footprint token grammar

Each entry in `reads` / `writes` is one of:

- A **named arg** from the signature: `"self"`, `"szApp"` (must match a
  declared arg name; unknown name → registration error at parse time,
  same loud-fail discipline as a bad signature).
- A **positional arg**: `"arg0"`, `"arg1"`, ... (0-based, matches
  signature order). Allowed even when the arg is anonymous.
- The **return value**: the literal `"return"`.
- The wildcard `"*"`: "I touch everything" (explicit worst-case;
  equivalent to omitting the footprint, but self-documenting).

### 3.2 Default when omitted (CRITICAL — must not change)

If `reads`/`writes` are **omitted**, the engine assumes **worst-case**:
the hook reads AND writes `"*"` (everything). This means an
undeclared hook behaves EXACTLY like v1 (load-order-loses on any
overlap). This is the safety property: **opting out of footprint
declaration can never make conflict detection more permissive than
v1.** Authors who declare footprints OPT IN to finer-grained
coexistence; authors who don't are no worse off than today.

### 3.3 Implied footprint per mode (before footprint is declared)

When a footprint is omitted, the worst-case default (§3.2) applies. The
per-mode "natural" footprint below is documentation of intent and the
basis for a possible future "infer footprint from mode" convenience —
it is NOT auto-applied in the first implementation (omitted = `"*"`,
full stop, to keep the safety property simple):

| Mode | Natural reads | Natural writes | Calls original? |
|---|---|---|---|
| `before`  | args | args | yes (unless it returns a value) |
| `after`   | return (+ args, read-only) | return | n/a (runs after) |
| `around`  | `*` | `*` | callback decides |
| `replace` | author-declared | author-declared (defaults to `return`) | no |

`around` is ALWAYS treated as `*`/`*` (touches everything) and can
never participate in footprint-based coexistence — it can do anything
at runtime, so its footprint is unknowable. An `around` on a target
forces load-order-loses for any other hook on that target. (Document
this clearly so authors know `around` is the "exclusive" mode.)

## 4. Data structures (the contract sub-4 MUST leave in place)

sub-4's `src/hook_chain.cpp` defines, in its anonymous namespace, a
per-target chain. The future smart work extends — never restructures —
these. The MUSTs:

- **`ChainEntry` MUST carry the footprint.** sub-4 ships `ChainEntry`
  WITHOUT `reads`/`writes` members, but adding them is purely additive
  (two `std::vector<FootprintSlot>` fields). NO other field changes.
- **The chain MUST remain `std::vector<ChainEntry>` (N entries, any
  mode mix).** sub-4 MUST NOT collapse "the replace" into a single
  distinguished field (e.g. `ChainEntry* replaceEntry`). A target may,
  under the smart rule, hold MULTIPLE coexisting replaces with disjoint
  write-sets. The container already supports this; the v1 REJECTION of
  a second replace is policy in the conflict function (§5), not a
  structural limit. **This is the single most important MUST: if sub-4
  bakes "one replace max" into the data structure, the smart upgrade
  becomes a rewrite.**
- **The conflict decision MUST be one isolated function**, e.g.
  `bool CanCoexist(const ChainEntry& incoming, const ChainEntry& existing, std::string& whyNot)`,
  called pairwise by `Add()` against every entry already in the target's
  chain. v1's body is "signature-compatible AND neither is replace/around";
  the smart version replaces ONLY this function's body with footprint
  overlap analysis. `Add()` itself does not change.

```cpp
// Footprint slot identity. Added to ChainEntry by the smart work.
struct FootprintSlot {
    enum Kind { Arg, Return, Wildcard } kind;
    int argIndex;   // valid when kind==Arg (0-based, post-name-resolution)
};
struct Footprint {
    std::vector<FootprintSlot> reads;
    std::vector<FootprintSlot> writes;
    bool wildcard = true;  // true = omitted/"*" = worst case (§3.2)
};
// ChainEntry gains: Footprint footprint;  (default wildcard=true)
```

## 5. The compatibility predicate (the smart core)

`CanCoexist(incoming, existing)` returns true iff the two hooks can BOTH
run on the same target without one corrupting the other's assumptions.
The full matrix — this is the spec, implement exactly:

1. **If either is `around`** → NOT compatible (around = `*`/`*`,
   exclusive). load-order-loses.
2. **If either has `footprint.wildcard == true`** (omitted/`"*"`) →
   NOT compatible (worst-case assumption; §3.2). load-order-loses.
   This subsumes v1: undeclared hooks never coexist with anything they
   overlap, which for blunt v1 means anything at all.
3. **Otherwise, compute overlap on declared slots.** Let `W_i`/`R_i` be
   incoming writes/reads and `W_e`/`R_e` existing writes/reads. The two
   are compatible iff:
   - `W_i ∩ W_e == ∅` (no two hooks write the same slot — a true
     write-write conflict; this is the one hard incompatibility), AND
   - the chain's load order makes any read-write overlap
     (`W_i ∩ R_e` or `R_i ∩ W_e`) **well-defined** — i.e. the writer
     runs in a deterministic position relative to the reader. Because
     the chain is fully ordered by load order, EVERY pair already has a
     deterministic order, so read-write overlap is always well-defined
     and therefore ALLOWED. Only write-write overlap fails.

   Worked check against the §2 example:
   - A writes {x4,x5}, B reads {x1,x2,x3,x4} writes {y1,y2}.
   - `W_A ∩ W_B = {x4,x5} ∩ {y1,y2} = ∅` → no write-write conflict. ✓
   - `W_A ∩ R_B = {x4,x5} ∩ {x1,x2,x3,x4} = {x4}` → read-write overlap,
     but load order defines A-before-B or B-before-A deterministically
     → allowed. ✓
   - Result: **compatible, both chain.** Exactly the required behavior.

4. **Signature compatibility is orthogonal and still required.**
   Footprint coexistence does NOT relax §1.1's signature rule — two
   hooks that coexist by footprint must still marshal through one shared
   thunk, so their signatures must still be byte-compatible. (A future
   even-smarter step could give each hook its own marshaling view, but
   that is explicitly out of scope for this spec.)

### 5.1 Dispatch ordering with coexisting replaces

When multiple `replace` hooks coexist (disjoint writes), they all run in
the pre-phase in load order, and NONE calls the original (replace
semantics). Each writes only its declared write-slots. The "result" of
the call is assembled from each replace's writes; the return-value slot
is owned by whichever replace declares `"return"` in its writes (and §5
rule 3 guarantees at most one does, else write-write conflict). If no
coexisting replace writes `"return"`, the return value is whatever the
last-applicable default is — DOCUMENT this edge precisely when
implementing; it needs a deliberate decision (likely: a replace MUST
declare `"return"` in writes if it intends to set the result, else the
return is undefined and a WARN fires).

## 6. Author-facing semantics that need doc clarification

- **`before` + `replace` composition**: `before` runs first (mutates
  args), then `replace` runs WITH the mutated args, original skipped.
  The `replace` author must be aware their replacement may receive
  non-default args. `docs/lua/hook.md` MUST state this when the feature
  ships.
- **`around` is exclusive**: only one hook total on a target if any is
  `around`. Document so authors don't expect chaining with `around`.

## 7. Files that change when this is built

- `src/hook_payload.{h,cpp}`: add `Footprint footprint;` to
  `HookPayload`. (sub-4 ships HookPayload WITHOUT it; additive.)
- `src/lua_bind_hook.cpp`: parse `reads = {...}` / `writes = {...}`,
  resolve named slots against the parsed signature (error on unknown
  name), build `Footprint`, store on the payload. Omitted → `wildcard`.
- `src/hook_chain.{h,cpp}`: add `Footprint footprint;` to `ChainEntry`;
  replace the body of the isolated `CanCoexist` predicate with the §5
  matrix. `Add()` and the chain container are UNCHANGED.
- `docs/lua/hook.md`: document `reads`/`writes`, the §6 semantics, the
  `around`-is-exclusive rule, and a worked two-plugin coexistence
  example.
- A new test-suite plugin (`cap-XX-hook-coexist`) MUST ship with the
  feature (per the test-with-every-capability contract): two plugins
  declaring disjoint footprints on one target, asserting both fire.

## 8. Why deferred (not cut)

Per workspace priority #2 (Capability) this IS real, in-scope capability
— it is deferred only because it is genuine scope-creep until a TC
author needs it, NOT because it's hard. sub-4's blunt rule is working
and correct (compatible hooks already chain; only genuine conflicts
fail). This spec is the additive upgrade path, kept precise so the
eventual implementation is mechanical.

## 9. Related

- `[[plugin.dependencies]]` in kcdx.toml works TODAY at the loader
  level — a plugin can require another be loaded first. Solves "I need
  plugin X loaded to reference its exports" independently of hook
  chaining.
- See `hook-api-positional-shorthand.md` for the related
  `kcdx.hook(target, callback)` positional-shorthand future-work entry.
