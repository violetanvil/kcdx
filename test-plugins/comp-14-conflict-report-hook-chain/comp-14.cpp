// COMP-14 — GetConflictReport now reports kcdx.hook (hook_chain) entries,
// including the CanCoexist-rejected loser.
//
// The AP7 + docs-discipline close of the "GetConflictReport covers
// hook_chain" feature (steps 1-2: hook_chain records chain->rejected +
// GetParticipantsAtTarget; Thunk_GetConflictReport merges them as a third
// source). Pre-feature, GetConflictReport(va) only walked the legacy
// conflict_engine resolved patch/hook lists, so a kcdx.hook target
// returned 0 entries. This plugin proves the report now SEES the chain.
//
// === Test design — ONE plugin, TWO replaces, name-sort winner ========
//
// Two `replace` hooks (cap-36's kcdxHookInterface surface) on ONE
// plugin-local stub target Comp14_Target. Replace-vs-replace is exclusive
// in v1 (hook_chain.cpp CanCoexist §"v1 worst-case: replace/around ...
// cannot coexist"), so the two cannot share the chain — one wins, one is
// rejected. WHICH one wins is decided by the deferred apply pass, which
// orders queued entries by (priority asc, name asc) — NOT by Replace()
// call order (lua_registry.cpp). Both entries are this one plugin at the
// same priority, so the NAME tiebreak decides:
//
//   - "comp14_a_winner" sorts first -> applied first -> first-touch builds
//     the Chain, becomes the winning entry in chain->entries (applied=true).
//   - "comp14_b_loser" sorts second -> hits FindChain(targetVa) non-null ->
//     CanCoexist fails -> chain->rejected.push_back(...) -> the loser,
//     reported applied=false.
//
// The a_/b_ name prefixes are the deterministic lever: there is no per-hook
// priority field on kcdxHookOptions, so the name sort IS what pins the
// winner. (A first review caught this: naming them comp14_win/comp14_lose
// INVERTED the result — "comp14_lose" < "comp14_win", so the "lose" entry
// won the name sort. The names now match the actual winner/loser.)
// CanCoexist rejects ANY second exclusive hook regardless of priority
// (hook_chain.cpp:446-464). This is cap-20's precedent: two replaces in one
// plugin, the name-sort-first wins, the other is REJECTED.
//
// === The same-VA-space proof (the step-2 review flag) =================
//
// For a raw opts.address locator, hook_chain::ResolveLocator returns
// payload.address verbatim (hook_chain.cpp:1283-1284), so the chain is
// keyed on targetVa == &Comp14_Target. GetConflictReport(va) ->
// GetParticipantsAtTarget(va) -> FindChain(va) keys on the SAME va. So
// querying reinterpret_cast<uintptr_t>(&Comp14_Target) hits the same
// chain the two installs resolved to — this row IS the proof that
// GetConflictReport(va)'s va matches hook_chain's targetVa for the
// address-locator path.
//
// === Falsifiability ===================================================
//
// Pre-feature (steps 1-2 absent): GetConflictReport on a hook_chain
// target returns 0 -> this row FAILS (0 != 2). Post-feature it returns 2,
// one applied!=0 (winner) and one applied==0 (rejected loser) -> PASS.
// The values are distinguishable from every near-miss: 1 entry would mean
// the loser was discarded (the pre-step-1 behavior); 2-both-applied would
// mean CanCoexist wrongly let two replaces coexist; 0 would mean the
// third source loop never ran.
//
// === Lifecycle ========================================================
//
// kcdxPlugin_Load: cache (api, self, logger), QueryInterface(Hook) +
//   QueryInterface(Messaging) for the backstop, install the two replaces.
// kcdxPlugin_PostGameLoad: the apply pass is done by now (cap-36/39/40
//   timing); call GetConflictReport(&Comp14_Target) and assert. Sets
//   g_post_ran so the InputLoaded backstop no-ops.
// InputLoaded backstop: loud FAIL if PostGameLoad never fired (cap-36/39
//   pattern) — no silent PENDING row.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {

const char* kName = "comp_14_conflict_report_hook_chain";
const char* kRow  = "COMP-14-conflict-report";

const kcdxInterface*     g_api  = nullptr;
const kcdxHookInterface* g_hook = nullptr;
kcdxPluginHandle         g_self = kcdxInvalidPluginHandle;
kcdxLogger               g_log;

bool g_post_ran = false;

// The two install handles (for diagnostics on FAIL).
kcdxHookHandle g_h_win  = 0;
kcdxHookHandle g_h_lose = 0;

// === The shared replace target — a plugin-local stub ==================
//
// noinline + a volatile tag so /OPT:ICF cannot fold it with any other
// byte-identical stub to a shared VA (the cap-20 / cap-36 lesson). Both
// replaces target THIS function via opts.address; GetConflictReport keys
// on its VA. Semantics are irrelevant — neither replace's body runs in
// this test (we only query the report, we don't invoke the stub), but it
// must be a real hookable function so the chain installs.
extern "C" __declspec(noinline) int Comp14_Target(int seed) {
    volatile int s = seed; volatile int unique = 0x1401; (void)unique;
    return s + 100;
}

// Two replace callbacks (distinct functions, distinct return values, so
// an accidental fold or mis-wire would be observable). Replace shape:
// <typed_return> cFn(/* typed args... */) — no prepended args.
extern "C" int Comp14_Win_Cb(int seed)  { (void)seed; return 7;  }
extern "C" int Comp14_Lose_Cb(int seed) { (void)seed; return 99; }

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("COMP14", "PASS %s: %s", kRow, reason);
    else      g_log.Error("COMP14", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// Install both replaces on Comp14_Target. The deferred apply pass orders
// entries by (priority asc, name asc) — NOT registration/call order
// (lua_registry.cpp). Both entries are this one plugin at the same
// priority, so the NAME tiebreak decides: "comp14_a_winner" sorts before
// "comp14_b_loser", so a_winner does first-touch (builds the chain → the
// applied winner) and b_loser hits FindChain-non-null → CanCoexist rejects
// it (replace-vs-replace is exclusive, priority-independent) → recorded in
// chain->rejected (applied=false). The a_/b_ name prefixes make that
// ordering explicit — there is no per-hook priority knob on
// kcdxHookOptions, so the name IS the deterministic lever. Both install
// CALLS return a non-zero handle: the loser's is a valid Failed handle
// (IsApplied==false), distinct from a registration error (returns 0).
bool InstallReplaces() {
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin = g_self;
        opts.address      = reinterpret_cast<uintptr_t>(&Comp14_Target);
        opts.signature    = "i32 (i32 seed)";
        opts.name         = "comp14_a_winner";   // sorts first → first-touch → applied
        g_h_win = g_hook->Replace(/*target=*/nullptr,
                                  (void*)&Comp14_Win_Cb, &opts);
    }
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin = g_self;
        opts.address      = reinterpret_cast<uintptr_t>(&Comp14_Target);
        opts.signature    = "i32 (i32 seed)";
        opts.name         = "comp14_b_loser";    // sorts second → CanCoexist-rejected
        g_h_lose = g_hook->Replace(/*target=*/nullptr,
                                   (void*)&Comp14_Lose_Cb, &opts);
    }
    g_log.Info("COMP14",
               "installed two replaces on Comp14_Target (0x%p): a_winner h=%llu "
               "b_loser h=%llu (b_loser is expected to be a Failed handle — "
               "CanCoexist-rejected, but still a non-zero handle id)",
               (void*)&Comp14_Target,
               (unsigned long long)g_h_win, (unsigned long long)g_h_lose);
    return g_h_win != 0 && g_h_lose != 0;
}

void RunAssertion() {
    const uintptr_t target = reinterpret_cast<uintptr_t>(&Comp14_Target);

    kcdxConflictEntry entries[8] = {};
    uint32_t count = g_api->GetConflictReport(
        target, entries, sizeof(entries) / sizeof(entries[0]));

    if (count != 2) {
        char r[256];
        snprintf(r, sizeof(r),
            "GetConflictReport(0x%p) returned %u entries (expected 2: the "
            "comp14_a_winner winner + the CanCoexist-rejected comp14_b_loser). "
            "0 would mean the hook_chain third source never ran (the pre-"
            "feature behavior); 1 would mean the rejected loser was "
            "discarded (pre-step-1)",
            (void*)target, count);
        Report(false, r);
        return;
    }

    // Classify the two entries by applied flag + name. Exactly one winner
    // (applied != 0, name comp14_a_winner — it sorts first, does first-touch)
    // and one loser (applied == 0, name comp14_b_loser — CanCoexist-rejected),
    // both kind == Hook.
    int  winners = 0, losers = 0;
    bool winNamed = false, loseNamed = false;
    bool allHookKind = true;
    char names[160] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        const kcdxConflictEntry& e = entries[i];
        if (e.kind != kcdxConflictEntryKind_Hook) allHookKind = false;
        if (e.applied) {
            ++winners;
            if (e.name && std::strcmp(e.name, "comp14_a_winner") == 0) winNamed = true;
        } else {
            ++losers;
            if (e.name && std::strcmp(e.name, "comp14_b_loser") == 0) loseNamed = true;
        }
        size_t used = std::strlen(names);
        snprintf(names + used, sizeof(names) - used, "%s%s(applied=%d,kind=%d)",
                 used ? ", " : "", e.name ? e.name : "<null>",
                 e.applied, e.kind);
    }

    const bool pass = (winners == 1) && (losers == 1) && allHookKind &&
                      winNamed && loseNamed;
    char r[400];
    snprintf(r, sizeof(r),
        "%s — GetConflictReport(0x%p)=2 entries: [%s]. Expected exactly one "
        "winner (applied!=0, name=comp14_a_winner) + one rejected loser "
        "(applied==0, name=comp14_b_loser), both kind=Hook. winners=%d losers=%d "
        "winNamed=%d loseNamed=%d allHook=%d",
        pass ? "report SEES the hook_chain winner + rejected loser"
             : "report did NOT match the winner+loser shape",
        (void*)target, names, winners, losers,
        winNamed ? 1 : 0, loseNamed ? 1 : 0, allHookKind ? 1 : 0);
    Report(pass, r);
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported the row.
    Report(false,
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase C++ export was not dispatched; the "
        "row reported FAIL via the InputLoaded backstop");
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    g_hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
    if (!g_hook) {
        api->ReportTestResult(g_self, kRow, 0,
            "QueryInterface(Hook) returned null at Plugin_Load — cannot "
            "install the two replaces the report is supposed to enumerate");
        return true;
    }

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnMessage);
    } else {
        g_log.Warn("INIT",
            "QueryInterface(Messaging) null — InputLoaded backstop disabled "
            "(if PostGameLoad never fires the row sits silent-PENDING)");
    }

    if (!InstallReplaces()) {
        api->ReportTestResult(g_self, kRow, 0,
            "one of the two Replace install CALLS returned a 0 handle (a "
            "registration error, distinct from the loser's valid-but-Failed "
            "handle) — see the COMP14 engine log for the teaching error");
        return true;
    }
    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // cached as g_api in Load.
    g_post_ran = true;
    g_log.Info("COMP14",
               "kcdxPlugin_PostGameLoad — apply pass done; querying "
               "GetConflictReport on the two-replace target");
    RunAssertion();
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
