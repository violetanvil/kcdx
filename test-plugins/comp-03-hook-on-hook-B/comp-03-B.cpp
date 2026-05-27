// COMP-03 — Cross-plugin hook-on-hook: two kcdx.hook{replace} on one named
// site; the lower-PLUGIN-priority plugin wins, the higher is rejected.
//
// Migration off the legacy [[hook]] bytes= first-wins path.
// Plugin A (pure Lua, [load_order].priority=10) and this plugin B (this DLL,
// [load_order].priority=20) both install a `replace` at the SAME named function
// entry: `IsInCombat_callsite_with_stack_frame` (Address Library id 1007,
// RVA 0x566040). Replace-vs-replace is exclusive in the hook_chain
// (CanCoexist rejects the second touch), so one wins and one is rejected.
//
// WHICH one wins is the cross-plugin apply order: the deferred apply pass
// sorts entries by (PLUGIN load-order priority asc, name asc) —
// lua_registry.cpp:429-447 reads kcdx::load_order::Of(pluginName).priority,
// i.e. the plugin's [load_order].priority. A=10 < B=20, so A's entry sorts
// first → A does the first-touch (the applied winner); B hits
// FindChain-non-null → CanCoexist fails → recorded in chain->rejected
// (applied=false). The PLUGIN priority is the deterministic lever.
//
// This DLL installs B's half via kcdxHookInterface::Replace on the NAMED
// target (cap-36 / comp-14 idiom), then in the after-phase:
//   - resolves the target VA via api->ResolveAddressByName(
//     "IsInCombat_callsite_with_stack_frame") — the SAME VA the kcdx.hook
//     target= name resolved to (the engine-seed name path; comp-14 proved
//     the address-locator VA match, this is the name path);
//   - calls api->GetConflictReport(va) and asserts:
//       * exactly 2 entries
//       * one named "comp03_a" with applied != 0 (A, the winner)
//       * one named "comp03_b" with applied == 0 (B, CanCoexist-rejected)
//       * both kind == Hook
//
// The observable is the conflict report read in the after-phase (final
// after the apply pass) — NOT a hook firing. Neither replace's body needs
// to run for this assertion (the comp-14 lesson: query the report, don't
// fire the chain). On match → COMP-03 PASS; on mismatch → FAIL with a
// human-readable rundown of the entries actually returned. An InputLoaded
// backstop reports loud FAIL if the after-phase never fired.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "kcdx/Interfaces.h"

namespace {

const char* kName = "comp_03_b";
const char* kRow  = "COMP-03";

const kcdxInterface*     g_api  = nullptr;
const kcdxHookInterface* g_hook = nullptr;
kcdxPluginHandle         g_self = kcdxInvalidPluginHandle;
kcdxLogger               g_log;

bool           g_post_ran = false;
kcdxHookHandle g_h_b      = 0;   // B's replace handle (for diagnostics)

// === The replace callback ============================================
//
// REPLACE shape: <typed_return> cFn(/* typed args... */). Signature is
// `bool (ptr self)` — matched to the seed AOB for id 1007 (the `mov
// rax,[rcx+8]` reads through rcx as a this/object pointer → 1 ptr arg;
// the trailing `3C 01` cmp al,1 tests the result as a byte → bool). The
// SAME signature plugin A uses. Returns false (0) — the migration of the
// legacy `31 C0 C3` (xor eax,eax; ret). B is CanCoexist-rejected, so this
// body never actually runs; it must exist with the right shape so the
// install is well-formed.
extern "C" bool Comp03_B_Replace_Cb(void* self) {
    (void)self;
    return false;
}

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("COMP03", "PASS %s: %s", kRow, reason);
    else      g_log.Error("COMP03", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// Install B's replace on the NAMED target. The name resolves the address
// (id 1007); opts.signature carries the ABI because the seed row has no
// signature for 1007 (the same one A supplies). opts.owningPlugin = g_self
// threads B's identity for the self-tier of self > engine > other. The
// install CALL returns a non-zero handle even though B loses the conflict
// (the loser's is a valid Failed handle — IsApplied==false — distinct from
// a 0 registration error).
bool InstallReplace() {
    kcdxHookOptions opts = {};
    opts.owningPlugin = g_self;
    opts.signature    = "bool (ptr self)";
    opts.name         = "comp03_b";
    g_h_b = g_hook->Replace("IsInCombat_callsite_with_stack_frame",
                            (void*)&Comp03_B_Replace_Cb, &opts);
    g_log.Info("COMP03",
               "installed B's replace on 'IsInCombat_callsite_with_stack_"
               "frame' (h=%llu); expected to be CanCoexist-rejected behind "
               "A (plugin priority 10 < B's 20)",
               (unsigned long long)g_h_b);
    return g_h_b != 0;
}

void RunAssertion() {
    // Resolve the target VA by NAME — the SAME engine-seed name the
    // kcdx.hook target= resolved to. Anonymous form is the engine-seed
    // path, which is exactly what an engine name (id 1007) resolves on.
    uintptr_t target =
        g_api->ResolveAddressByName("IsInCombat_callsite_with_stack_frame");
    if (target == 0) {
        Report(false,
            "ResolveAddressByName('IsInCombat_callsite_with_stack_frame') "
            "returned 0 — the engine-seed name did not resolve on this "
            "build, so the conflict report cannot be queried at the chain's "
            "target VA");
        return;
    }

    kcdxConflictEntry entries[8] = {};
    uint32_t count = g_api->GetConflictReport(
        target, entries, sizeof(entries) / sizeof(entries[0]));

    if (count != 2) {
        char r[256];
        snprintf(r, sizeof(r),
            "GetConflictReport(0x%p) returned %u entries (expected 2: the "
            "comp03_a winner + the CanCoexist-rejected comp03_b). 0 would "
            "mean the hook_chain was never built at this VA; 1 would mean "
            "the rejected loser was not reported",
            (void*)target, count);
        Report(false, r);
        return;
    }

    // Classify: exactly one winner (applied != 0, name comp03_a) + one
    // loser (applied == 0, name comp03_b), both kind == Hook.
    int  winners = 0, losers = 0;
    bool aWonNamed = false, bLostNamed = false;
    bool allHookKind = true;
    std::string names;
    for (uint32_t i = 0; i < count; ++i) {
        const kcdxConflictEntry& e = entries[i];
        if (e.kind != kcdxConflictEntryKind_Hook) allHookKind = false;
        if (e.applied) {
            ++winners;
            if (e.name && std::strcmp(e.name, "comp03_a") == 0) aWonNamed = true;
        } else {
            ++losers;
            if (e.name && std::strcmp(e.name, "comp03_b") == 0) bLostNamed = true;
        }
        if (!names.empty()) names += ", ";
        names += e.name ? e.name : "<null>";
        names += "(";
        names += (e.kind == kcdxConflictEntryKind_Patch) ? "patch" : "hook";
        names += "=";
        names += e.applied ? "OK" : "ABORTED";
        names += ")";
    }

    const bool pass = (winners == 1) && (losers == 1) && allHookKind &&
                      aWonNamed && bLostNamed;
    char r[400];
    snprintf(r, sizeof(r),
        "%s — cross-plugin replace-reject at 0x%p (A prio 10 < B prio 20): "
        "[%s]. Expected one winner (applied!=0, name=comp03_a) + one "
        "rejected loser (applied==0, name=comp03_b), both kind=Hook. "
        "winners=%d losers=%d aWon=%d bLost=%d allHook=%d",
        pass ? "report SEES A applied + B rejected"
             : "report did NOT match the A-wins/B-rejected shape",
        (void*)target, names.c_str(), winners, losers,
        aWonNamed ? 1 : 0, bLostNamed ? 1 : 0, allHookKind ? 1 : 0);
    Report(pass, r);
}

void OnInputLoaded(kcdxMessage* msg) {
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
            "install B's replace; the cross-plugin conflict cannot form");
        return true;
    }

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnInputLoaded);
    } else {
        g_log.Warn("INIT",
            "QueryInterface(Messaging) null — InputLoaded backstop disabled "
            "(if PostGameLoad never fires the row sits silent-PENDING)");
    }

    if (!InstallReplace()) {
        api->ReportTestResult(g_self, kRow, 0,
            "B's Replace install CALL returned a 0 handle (a registration "
            "error, distinct from the loser's valid-but-Failed handle) — see "
            "the COMP03 engine log for the teaching error");
        return true;
    }
    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // cached as g_api in Load.
    g_post_ran = true;
    g_log.Info("COMP03",
               "kcdxPlugin_PostGameLoad — apply pass done; resolving the "
               "named target VA and querying GetConflictReport");
    RunAssertion();
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
