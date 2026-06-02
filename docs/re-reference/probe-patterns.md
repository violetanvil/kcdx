# Probe patterns

Common shapes for kcdx debug probes. Each: question it answers, code skeleton, interpretation rule.

Assumes `#include "log.h"` for `LOG_DEBUG_KV`, `LOG_WARN`, `log::KV(...)`. Emit a stable category tag per probe (`MID_HOOK`, `SCRIPTING`, etc.) so the dev log can be filtered.

---

## Pattern 1: Read-only observation

**Question:** *"What is the state of X at this point?"*

**Cost:** lowest possible. No allocations, no side effects.

```cpp
// === DIAGNOSTIC (PROBE D): is captured L the main thread or a coroutine?
int status = lua_status(L);
int is_main = lua_pushthread(L);
lua_pop(L, 1);
int top = lua_gettop(L);
LOG_DEBUG_KV("CATEGORY", "probe_d.state_identity",
    log::KV("L",              (void*)L),
    log::KV("status",         (int64_t)status),
    log::KV("is_main_thread", (int64_t)is_main),
    log::KV("top",            (int64_t)top));
```

**Interpretation:** read-only probe + bug still reproduces → bug doesn't depend on observation; you've ruled out a category and learned the state. Read-only probe + outcome changes → your "read" is doing more than reading (rare with `lua_get*`/`lua_to*`; common with `lua_topointer` on certain stack indices that promote values).

Always start a new investigation with read-only probes if possible.

---

## Pattern 2: Bypass an entire subsystem

**Question:** *"Does X being active matter at all?"*

**Cost:** one launch. Single call-site change.

```cpp
// === DIAGNOSTIC (PROBE G): bypass kcdx::console::Init() entirely.
// If save-load still corrupts with this bypass, console init is innocent.
LOG_WARN("LUA_BIND",
    "PROBE_G: bypassing kcdx::console::Init() — cap-13 will fail.");
// kcdx::console::Init();
```

Disciplines:
1. **State the negative consequence in the log line.** "cap-13 will fail" tells future-you the failure is expected.
2. **Don't bypass two things at once.** Revert prior bypass before adding the next, unless explicitly investigating their interaction.

**Interpretation:** bypass-still-crashes → subsystem innocent. Bypass-fixes → subsystem is doing the harmful thing; bisect within.

**On bug close, archive (NEVER revert).** The bypass becomes `#if 0` with the four-line archive header per `debug/SKILL.md` §3d. Bypasses cannot be `durable` (they would un-init the subsystem at every launch) — `archived` is the only resting state.

---

## Pattern 3: Canary / fingerprint to detect memory clobbering

**Question:** *"Is this memory block being overwritten between time A and time B?"*

**Cost:** one method + N log call sites. Pure-read at every checkpoint.

```cpp
uint64_t MyObject::fingerprint_self() const {
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(this);
    for (size_t i = 0; i < sizeof(*this); ++i) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

LOG_DEBUG_KV("CATEGORY", "fingerprint.checkpoint_name",
    log::KV("object", (void*)obj),
    log::KV("fnv1a",  obj->fingerprint_self()));
```

**Interpretation:** fingerprint changes → block being written by someone (diff the bytes if you can dump them). Fingerprint identical → block intact; corruption elsewhere.

**Durable** — keep fingerprint methods enabled past bug close (cost nothing when not called). Record in the known-issue's "Active diagnostic instrumentation" table with Status: `durable`. Bypasses are NEVER durable — they go to Status: `archived` (compile-disabled `#if 0` per `debug/SKILL.md` §3d; revival would re-enable the bypass deliberately, which the `#if 0` makes explicit). Reverting / deleting any probe is forbidden (CLAUDE.md hard rule).

---

## Pattern 4: Time-bisect against a known-good commit

**Question:** *"Did this bug exist at commit C, or is it a regression?"*

**Cost:** one worktree + one extra build. Highest insight per probe.

Agent-run sequence (you, not the user — per `agent-builds-and-deploys.md`):

```bash
git worktree add -d /tmp/kcdx-prev <commit-sha>
# Apply minimal reproducer probe (don't backport whole investigation)
cd /tmp/kcdx-prev && pwsh ./build.ps1
cp build/Release/kcdx.dll <game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/kcdx.dll
# Hand the user the launch; they reproduce; you read the log
git worktree remove --force /tmp/kcdx-prev
```

Disciplines:
1. **Disable plugins for isolation.** Old `kcdx.dll` against today's plugins → ABI mismatch noise. Write a temporary `kcdx-engine/load_order.toml` with `[[plugin]] name=... enabled=false` for each plugin you want skipped, OR simply move the plugin folders out of `kcdx-plugins/` for the isolation run.
2. **Don't commit to the worktree.** Throwaway. Findings go in the live tree's known-issue file.

**Interpretation:** bug-reproduces-at-known-good → bug older than expected; "regression" frame is wrong. Bug-doesn't-reproduce → binary-search commits between known-good and HEAD.

---

## Pattern 5: ABI / pointer-identity check

**Question:** *"Are we using the same handle/state/pointer the rest of the system uses, or did we capture the wrong one?"*

**Cost:** medium — depends on whether the system's own accessor is reachable.

```cpp
// === DIAGNOSTIC (PROBE H): every distinct lua_State* through HookedLuaPcall
static std::atomic<lua_State*> seen[8] = {};
static std::atomic<int>        seen_n{0};
bool new_L = true;
for (int i = 0; i < 8; ++i) {
    lua_State* s = seen[i].load(std::memory_order_relaxed);
    if (s == L) { new_L = false; break; }
    if (!s) {
        lua_State* expected = nullptr;
        if (seen[i].compare_exchange_strong(expected, L,
                                            std::memory_order_acq_rel)) {
            seen_n.fetch_add(1);
        }
        break;
    }
}
if (new_L) {
    LOG_DEBUG_KV("CATEGORY", "lua_pcall.new_L_seen",
        log::KV("L",      (void*)L),
        log::KV("seen_n", (int64_t)seen_n.load()));
}
```

**Interpretation:** `seen_n` stays 1 → one canonical handle, you've got the right one. `seen_n` climbs → multiple exist; check whether your operation should target a different one.

---

## Pattern 6: Reverse-order interaction probe

**Question:** *"Does the order of A and B matter, or is the bug intrinsic to B regardless of A?"*

**Cost:** code reorder + one launch.

If `A; B` corrupts and `B` is the suspect, run `B; A`. Still corrupts → B's intrinsic. Now clean → A was priming B's failure mode.

```cpp
// Original: A; B
// PROBE L: B; A
PROBE_E(L);
RegisterKcdxTable(L);
```

---

## Picking the next probe

When the Trail has eliminated several hypotheses, prefer in this order:

1. **Read-only** that would falsify a remaining hypothesis (cheapest).
2. **Bypass** of a suspected subsystem (one launch).
3. **Fingerprint** at suspected-fragile checkpoints (durable).
4. **Time-bisect** against known-good (highest insight, ~2 launches).
5. **Reverse-order** if interaction between two operations is suspect.

Avoid "let me add 10 log lines and see" — that's a search, not a probe. Each probe has one question. If you don't have a question, go back to Phase 1.
