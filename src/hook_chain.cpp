// kcdx::hook_chain — per-target chain of kcdx.hook callbacks. See
// hook_chain.h for the model + conflict policy, and
// smart-replace conflict detection (future work) for the
// footprint-coexistence upgrade this architecture is built to accept.
//
// This file is the NEW kcdx.hook dispatch path. The legacy
// kcdx::scripting dynamic_hook_pre/post path is reference-only and will
// be removed once this is verified.
//
// Structure of this TU:
//   §1  signature -> make_jit_func type-string conversion
//   §2  ChainEntry + Chain data model (footprint-ready per the spec)
//   §3  CanCoexist — the isolated conflict predicate (v1 blunt body)
//   §4  named-arg `args` table construction (the UX surface)
//   §5  call_original bridge over MinHook's pOriginal (true around)
//   §6  DispatchPre / DispatchPost — the C callbacks the JIT thunk calls
//   §7  Add — locator resolve, first-touch install, append to chain

#include "hook_chain.h"

#include <cstdint>
#include <cstdio>    // snprintf (callsite diagnostics)
#include <cstdlib>   // strtoull (callsite rva parse)
#include <cstring>   // memcpy (callsite E8 displacement read/write)
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>  // WideCharToMultiByte / MultiByteToWideChar

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <asmjit/asmjit.h>

#include "address_library.h"   // ResolveByName (address_id = "name")
#include "refdb.h"             // ResolveAddrById (address_id = numeric kcdx_id)
#include "dynamic_call_jit.h"  // BuildLuaCallThunk (call_original over pOriginal)
#include "hde/hde64.h"         // hde64_disasm (auto-decode mid resume offset)
#include "hook_engine.h"       // InstallRuntime
#include "log.h"
#include "lua_bind_helpers.h"  // PushPointer
#include "lua_memory.h"        // pointer, kPointerMetatable
#include "modification_inventory.h"  // RecordFire (per-detour fire breadcrumb)
#include "patch_engine.h"      // Resolve, ResolvedPatch (locator pipeline)
#include "pe_helpers.h"        // OpenModule (callsite rva -> module base)
#include "rom_borrowed/runtime_func_t.h"
#include "rom_borrowed/type_info_t.h"

namespace kcdx::hook_chain {

// Forward decl (definition near the bottom of this TU, alongside the
// public kcdx::hook_signature::SignaturesCompatible wrapper it backs).
// Declared here so the in-TU chain-share check (§3, Add/AddC) below can
// call it by unqualified lookup before the definition appears.
bool SignaturesCompatibleImpl(const kcdx::hook_signature::Signature& a,
                              const kcdx::hook_signature::Signature& b);

namespace {

// ===========================================================================
// §1  signature -> make_jit_func type strings
// ===========================================================================
//
// make_jit_func consumes return/param type STRINGS, parsed by
// kcdx::rom::get_type_info_from_string into type_info_t. Map each parsed
// hook_signature::Type to the canonical string that resolves to the
// right type_info_t (see rom_borrowed/type_info_t.cpp's matcher):
//   string_  <- "string" / "const char*"
//   boolean_ <- "bool"
//   ptr_     <- "ptr"
//   float_   <- "float"
//   double_  <- "double"
//   integer_ <- anything else (default)

const char* SigTypeToJitString(kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Void: return "void";
        case T::Bool: return "bool";
        case T::F32:  return "float";
        case T::F64:  return "double";
        case T::Ptr:  return "ptr";
        // Wide/narrow C strings both marshal as a char* the wrapper
        // pushes as a Lua string. (wstr write-back nuance is handled by
        // the value_wrapper at :set() time; see lua_memory.)
        case T::Wstr: return "string";
        case T::Cstr: return "string";
        // Integer widths must be PRESERVED, not collapsed to i64. The
        // call_original thunk (BuildLuaCallThunk) emits an asmjit call to
        // the original function with this exact ABI signature — calling
        // an int32-returning function as int64 reads the return from the
        // full RAX when the callee only wrote EAX (upper bits undefined),
        // corrupting the result. (Root cause of an around-mode hook returning
        // 0 when wrapping an int32 original.)
        // get_type_id maps each of these to the right asmjit width.
        case T::I8:  return "i8";
        case T::I16: return "i16";
        case T::I32: return "i32";
        case T::I64: return "i64";
        case T::U8:  return "u8";
        case T::U16: return "u16";
        case T::U32: return "u32";
        case T::U64: return "u64";
        default:     return "i64";
    }
}

// Build the (return_type, param_types) string pair make_jit_func wants
// from a parsed signature.
void SignatureToJitStrings(const kcdx::hook_signature::Signature& sig,
                           std::string&              returnTypeOut,
                           std::vector<std::string>& paramTypesOut) {
    returnTypeOut = SigTypeToJitString(sig.returnType);
    paramTypesOut.clear();
    paramTypesOut.reserve(sig.args.size());
    for (const auto& a : sig.args) {
        paramTypesOut.emplace_back(SigTypeToJitString(a.type));
    }
}

// ===========================================================================
// §2  ChainEntry + Chain data model
// ===========================================================================
//
// Footprint-readiness: ChainEntry is a value in a
// std::vector<ChainEntry> — N entries, any mode mix. There is NO
// distinguished "the replace" slot. The smart-coexistence upgrade adds
// a `Footprint footprint;` member here and swaps the body of CanCoexist
// (§3); nothing else changes. DO NOT collapse replace into a single
// field — that turns the upgrade into a rewrite.

struct ChainEntry {
    // Tagged-union discriminant: Lua-kind entries carry a Lua callback
    // ref; C-kind entries carry a raw C function pointer + the parsed
    // signature so dispatch can call it with the right per-slot widths.
    // Default Kind::Lua so the four existing AddCallsite/Add construction
    // sites (which set mode / callbackRef / pluginName / priority / name /
    // handleId only) keep producing a Lua entry without code change.
    //
    // The C-kind branch in DispatchPre/DispatchPost/DispatchExclusive +
    // MidDispatch is a defensive warn-and-skip stub in this chunk —
    // BuildCCallThunk (chunk 2) and AddC + hook_interface.cpp thunks
    // (chunks 3+4) are what actually populate it. Until those land, no
    // C-kind entry can reach a dispatcher (no constructor path produces
    // one); the stub is a future-not-yet-wired greppable safety net.
    enum class Kind : uint8_t { Lua = 0, C = 1 };
    Kind         kind = Kind::Lua;

    kcdx::hook_payload::Mode mode = kcdx::hook_payload::Mode::Before;

    // --- Lua-kind fields (populated when kind == Kind::Lua) ------------
    int          callbackRef = -2;  // LUA_NOREF; the Lua callback closure

    // --- C-kind fields (populated when kind == Kind::C) ----------------
    // cFn is the author's raw C callback (kcdxHookInterface install path,
    // chunks 3+). cSig is a COPY of the parsed signature taken at append
    // time so the dispatcher can marshal per-slot widths without
    // re-resolving anything. Default-constructed (cFn=nullptr, empty
    // signature) for Lua entries — the empty Signature is cheap (vector
    // + two strings) and never read on the Lua path.
    void*                           cFn = nullptr;
    kcdx::hook_signature::Signature cSig{};

    // C dispatch thunk emitted at AddC time via BuildCDispatchThunk. The
    // engine→callback marshaling trampoline that unpacks parameters_t
    // slots into host x64 registers per cSig and invokes cFn with the
    // per-mode ABI (Before: args[]+outCount+typed; After: typed_return
    // origReturn + typed args; Around: typed call_original + typed args;
    // Replace: typed args returning typed_return; Mid: handled on Chain
    // via midCDispatchThunk). Null for Kind::Lua entries.
    void* cDispatchThunk = nullptr;

    // --- Shared (both kinds) -------------------------------------------
    std::string  pluginName;        // owning plugin (load-order attribution)
    int          priority   = 50;   // effective load-order priority
    std::string  name;              // author name (diagnostics)
    // The registry handle id (lua_registry::Entry::handleId) that produced
    // this entry. Stamped at Add time so Uninstall(handleId) finds the
    // matching ChainEntry to erase without an extra index. 0 only for
    // legacy / not-yet-threaded entries (defensive — production Adds carry
    // the real id from lua_bind_hook::ApplyHookEntry).
    uint64_t     handleId    = 0;

    // Off-thread routing policy for THIS entry (copied from
    // HookPayload::offThread at append time). 0 = Marshal (degraded to
    // Skip-with-warn-once per Outcome P in v1; real arg-snapshot marshal
    // is its own future cycle when the warn ever fires). 1 = Skip
    // explicit (same shape; the explicit-Skip option distinguishes
    // "asked for skip" from "asked for marshal but got Skip-degradation").
    // 2 = Error log per-fire + skip. Values match kcdxHookOffThread_*.
    // The engine auto-marshals off-thread hits to the main thread.
    uint8_t      offThread = 0;

    // True iff this entry was registered by the engine itself (via the
    // internal AddCEngine entry point) — the engine-direct migration moved
    // every production hook (lua_pcall, frealloc canary, ModManager_ctor,
    // BugSplat ctor, SaveGame, LoadGame) off raw MH_CreateHook onto AddC,
    // and they stamp this flag so InsertOrdered sorts engine entries to the
    // FRONT of the chain ahead of any plugin entry regardless of declared
    // priority. The engine-always-first invariant is type-enforced (not
    // priority-sentinel convention) so a plugin cannot impersonate engine
    // ordering by picking an extreme priority. Default false keeps every
    // existing plugin construction site unchanged.
    bool         isEngine = false;
};

// A kcdx.hook entry that LOST a CanCoexist conflict at this target and
// was therefore never appended to `entries`. The winning Add already
// returned its handle Failed (res.reason == this `reason`); we keep a
// minimal record here so the conflict report can list the loser with
// applied=false (kcdxConflictEntry.applied "0 = aborted (lost a
// conflict)", Interfaces.h) — mirroring what conflict_engine's
// g_resolvedHooks does for the legacy hook path. `applied` is
// implicitly false (these are the losers). The record grows only on a
// (rare) rejection; an uncontested chain carries none.
struct RejectedEntry {
    std::string name;
    int         priority = 50;
    std::string reason;  // the SAME string handed to res.reason / handle:reason()
};

// One hooked target. Owns the JIT trampoline (runtime_func_t) for its
// lifetime + the ordered callback chain. The runtime_func_t holds the
// MinHook detour; destroying it would uninstall the hook, which we never
// do (hooks live for the session, matching SKSE).
//
// A mid Chain (isMid) is a DIFFERENT install: a mid-function detour at
// the captured-instruction VA (targetVa already includes `offset`), built
// with make_jit_midfunc + the MidDispatch C callback, carrying its own
// capture layout instead of a function signature. v1 keeps one mid hook
// per VA (the JIT bakes one capture layout); a second mid hook on the
// same VA loses by load order (CanCoexist's mid branch). The struct is
// the same shape as a signature Chain so the map + lifetime rules are
// shared; the mid-only fields sit alongside.
struct Chain {
    uintptr_t                                  targetVa = 0;
    std::unique_ptr<kcdx::rom::runtime_func_t> rf;
    // The signature the FIRST hook fixed; later hooks must be
    // SignaturesCompatible with this to share the thunk. Dispatch
    // marshals directly off this (per-slot hook_signature::Type), which
    // is why wstr/cstr round-trip correctly (the legacy type_info_t
    // can't tell them apart). Unused for mid chains.
    kcdx::hook_signature::Signature            sig;
    // call_original thunk over MinHook's pOriginal, built when the first
    // around on this target lands (a lua_CFunction; see §7). 0 = none.
    uintptr_t                                  callOriginalThunk = 0;
    // C analogue of callOriginalThunk: a native pass-through pointer
    // (from BuildNativeCallThunk in chunk 2) for C Around's
    // call_original primitive. The two are NOT interchangeable —
    // callOriginalThunk has lua_CFunction shape (Lua stack marshal);
    // callOriginalCThunk has the native typed signature shape. C Around
    // entries on the chain read this field; Lua Around entries read
    // callOriginalThunk. Built on first-touch C-Around per chain;
    // 0 = none.
    uintptr_t                                  callOriginalCThunk = 0;
    std::vector<ChainEntry>                    entries;       // load-order ordered

    // --- callsite-only -------------------------------------------------
    // A callsite Chain is keyed in g_chains by the CALL-INSTRUCTION VA
    // (not the callee VA): a callsite redirect affects ONE caller, so the
    // mediated resource is the call site's 4 rewritten displacement bytes,
    // not the callee. Two plugins redirecting the SAME call site collide
    // (load-order-loses via CanCoexist's callsite branch); two plugins
    // redirecting DIFFERENT call sites to the same callee do NOT — that is
    // the whole point of callsite vs function-entry. The dispatch spine
    // (DispatchPre/Post + the entries chain + sig + callOriginalThunk) is
    // shared with a function-entry Chain; only the install differs (an E8
    // rewrite instead of a MinHook detour on the callee).
    bool                     isCallsite = false;
    uintptr_t                calleeVa = 0;  // original callee (orig() target)

    // --- mid-only ------------------------------------------------------
    bool                     isMid = false;
    // Mid is one-per-VA in v1: the JIT bakes one capture layout per site,
    // so the mid callback lives on Chain itself (not in entries). To stay
    // parallel with the ChainEntry tagged union, mid carries a Kind
    // discriminant + a Lua-kind field (midCallbackRef) AND a C-kind pair
    // (midCFn + midCSig). Default Kind::Lua so AddMid (which sets
    // midCallbackRef + midPluginName + midName + midHandleId only) keeps
    // producing a Lua mid without code change. MidDispatch branches on
    // midKind; the C branch is a defensive warn-and-skip stub in this
    // chunk (the real wire-up is chunks 2+3, parallel to ChainEntry::C).
    ChainEntry::Kind         midKind = ChainEntry::Kind::Lua;
    int                      midCallbackRef = -2;  // LUA_NOREF
    // C-kind mid fields (populated when midKind == Kind::C; defaults
    // otherwise). cSig here is the mid callback's typed signature (the
    // shape make_jit_midfunc / the dispatcher use to marshal capture
    // slots into the C call frame); empty for Lua mids.
    void*                           midCFn = nullptr;
    kcdx::hook_signature::Signature midCSig{};
    // Mid C dispatch thunk emitted at AddCMid via BuildCDispatchThunk
    // with Mode::Mid. Marshals the JIT slot payload into a stack-
    // allocated kcdxHookCaptureValue[N] (typed per chain.capTypes[i]),
    // invokes midCFn(values, count), reads back typed values post-call
    // and writes the bytes back. Null for Kind::Lua mids.
    void*                           midCDispatchThunk = nullptr;
    std::string              midPluginName;
    std::string              midName;
    // Off-thread routing policy for this mid chain (mid is one-per-VA,
    // so the policy lives on Chain itself, parallel to midHandleId).
    // Same value semantics as ChainEntry::offThread. Copied from
    // HookPayload::offThread at AddMid / AddCMid time.
    uint8_t                  midOffThread = 0;
    // The registry handle id that produced this mid chain. Mid is one-
    // per-VA in v1, so the id lives on Chain itself (not in entries).
    // Uninstall(handleId) matches against midHandleId for mid chains.
    uint64_t                 midHandleId = 0;
    // Mid-level mirror of ChainEntry::isEngine — set true when the mid
    // chain was installed via AddCEngine (the engine-direct migration path).
    // Mid is one-per-VA in v1 so the engine flag lives on Chain itself,
    // parallel to midKind / midHandleId. Read by the off-thread carve-out
    // gate at MidDispatch (see the comment block at the gate) and by the
    // dead-classifier bypass.
    bool                     isMidEngine = false;
    // Parallel capture metadata (parsed by the binder). captureNames[i]
    // == "" means positional (handle table keyed 1..N); otherwise the
    // handle table is keyed by name. captureTypes drives per-slot
    // marshaling in MidDispatch (i8..u64 / ptr / f32 / f64).
    std::vector<std::string> capExprs;
    std::vector<std::string> capTypes;
    std::vector<std::string> capNames;

    // --- conflict losers ----------------------------------------------
    // kcdx.hook entries that lost a CanCoexist conflict at this target
    // (the winner is THIS chain). Empty for an uncontested target; grows
    // by one per rejected Add* at this VA. Read alongside `entries` by
    // GetParticipantsAtTarget to report winners (applied=true) + losers
    // (applied=false). See RejectedEntry above.
    std::vector<RejectedEntry> rejected;
};

// target VA -> Chain. Process-lifetime; node-stable via unique_ptr so
// the dispatchers can hold a raw Chain* across the hot path without the
// map rehashing it away.
std::unordered_map<uintptr_t, std::unique_ptr<Chain>> g_chains;

// Guards g_chains structure (Add appends; dispatch reads). The Lua-VM
// single-thread contract (the engine marshals off-thread hits to the main
// thread) means dispatch is main-thread-only, but Add can run during the
// first-tick registration pass; a coarse mutex keeps the map consistent.
// Dispatch takes it only to resolve target->Chain*, then releases before
// the lua_pcall (which can run arbitrary Lua).
std::mutex g_chainsMu;

// The live game lua_State, set by Install on first use (the chain
// dispatchers run callbacks against it). Mirrors scripting::lua_state();
// we capture our own copy so this module doesn't depend on the legacy
// scripting TU.
lua_State* g_L = nullptr;

// Thread-local stamp flag used ONLY by the engine-direct migration path.
// AddCEngine (defined below) sets this true on the calling thread, calls
// AddC's regular install path, and the four ChainEntry construction sites
// in AddC / AddCMid / AddCCallsite read this once + clear it to stamp the
// entry's isEngine = true. Thread-local so a concurrent plugin AddC on
// another thread cannot pick up the stamp. The flag is one-shot per
// AddCEngine call — read-and-clear at the first ChainEntry construction
// (the only one a single AddCEngine invocation reaches; AddC's three
// branches each construct exactly one entry).
thread_local bool t_addcEngineStamp = false;
inline bool TakeEngineStamp() {
    if (!t_addcEngineStamp) return false;
    t_addcEngineStamp = false;
    return true;
}

// Per-hook dedup for the off-thread Skip / Marshal-degraded warn-once
// line. Keyed by the
// ChainEntry::handleId for sig + callsite entries; the Mid path keys
// on Chain::targetVa (mid is one-per-VA in v1 so handleId is also
// stable, but targetVa makes the dedup intent self-evident and
// matches the brief's recommendation). Off-thread fires race across
// worker threads — guard with its own mutex; do NOT take g_chainsMu
// from this path (the dispatcher already releases it before
// lua_pcall, and a worker-thread skip should not block the main
// thread's chain resolve).
std::unordered_set<uint64_t> g_offThreadWarned;
std::mutex                   g_offThreadWarnedMu;

// True iff we should emit (and skip) for this entry given the policy +
// thread context. The caller already established off-thread. Returns
// true to mean "skip the callback" — the same outcome for all three
// policy values; only the log shape differs.
//
// Policy values:
//   0 (Marshal — degraded to Skip-with-warn-once per Outcome P):
//      warn-once per dedupKey, then skip.
//   1 (Skip explicit): warn-once per dedupKey, then skip.
//   2 (Error): log::ErrorF every fire (NOT deduped), then skip.
//
// In the current corpus (cap-15..22 + cap-35) no off-thread sites
// fire, so this path is a future-not-yet-wired safety net. When a real
// site lands, the Marshal degradation can be replaced with a true
// arg-snapshot Marshal in its own cycle.
bool OffThreadShouldSkip(uint8_t policy, uint64_t dedupKey,
                         const char* what, uintptr_t targetVa,
                         const char* nameForLog,
                         const char* pluginForLog) {
    if (policy == 2) {
        log::ErrorF("hook_chain: off-thread %s '%s' (plugin '%s') at 0x%p "
                    "fired on tid=%lu (policy=Error) — skipping",
                    what, nameForLog ? nameForLog : "",
                    pluginForLog ? pluginForLog : "",
                    (void*)targetVa, (unsigned long)::GetCurrentThreadId());
        return true;
    }
    // Policy 0 (Marshal degraded) and 1 (Skip explicit) both warn-once.
    bool firstTime;
    {
        std::lock_guard<std::mutex> lock(g_offThreadWarnedMu);
        firstTime = g_offThreadWarned.insert(dedupKey).second;
    }
    if (firstTime) {
        log::WarnF("hook_chain: off-thread %s '%s' (plugin '%s') at 0x%p "
                   "fired on tid=%lu (policy=%s) — skipping (warn-once-"
                   "per-hook). v1 Marshal degrades to Skip; a real "
                   "arg-snapshot Marshal is a future cycle if this warn "
                   "ever fires in practice.",
                   what, nameForLog ? nameForLog : "",
                   pluginForLog ? pluginForLog : "",
                   (void*)targetVa, (unsigned long)::GetCurrentThreadId(),
                   policy == 0 ? "Marshal[degraded]" : "Skip");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dead-callback-ref warn/error-once latch (#3 + #13, Batch E fail-state sweep).
//
// A dispatch entry whose callbackRef is a REAL registry ref (>= 0) but whose
// lua_rawgeti yields a non-function means the closure was lost (GC'd, or the
// registry slot reused) WHILE the entry stayed live in the chain. Today's
// author surfaces cannot produce this — uninstall/fail always luaL_unref AND
// reset callbackRef to LUA_NOREF together (lua_bind_hook.cpp:257-260,
// 1089-1091), so a live entry carries either a valid function ref or the -2
// sentinel. It is a defensive guard against an INTERNAL lifecycle bug (a future
// unref-without-reset, registry reuse). Pre-Batch-E it was silent: an exclusive
// (replace/around) entry neutralized the function with ZERO log (#3); a
// before/after/mid went inert every fire with ZERO log (#13).
//
// Hot-path contract: these are
// per-call dispatch paths on per-frame game functions. The steady-state LIVE
// path (lua_isfunction passes) NEVER reaches this latch — zero log, zero alloc,
// zero lock. Only the FIRST observation of a lost ref, per dedup key, logs;
// thereafter the set lookup short-circuits. Same idiom + same keying as the
// off-thread warn-once above (g_offThreadWarned): handleId for sig/callsite
// entries, targetVa for mid (one-per-VA in v1). NOT a Lua static-const sentinel
// (use the live Lua C API, no kcdx-side sentinels) — a plain integer set guarded by a mutex.
std::unordered_set<uint64_t> g_deadRefWarned;
std::mutex                   g_deadRefWarnedMu;

// True iff THIS dedup key has not yet logged its dead-ref line. One insert per
// distinct key for the process lifetime; the common (live) path never calls
// this. severity is chosen by the caller: #3 (exclusive) logs Error (the
// function is neutralized — a wrong-result/crash-risk); #13 (before/after/mid)
// logs Warn (the entry merely goes inert — a degradation).
bool DeadRefFirstObservation(uint64_t dedupKey) {
    std::lock_guard<std::mutex> lock(g_deadRefWarnedMu);
    return g_deadRefWarned.insert(dedupKey).second;
}

Chain* FindChain(uintptr_t va) {
    auto it = g_chains.find(va);
    return it == g_chains.end() ? nullptr : it->second.get();
}

// ===========================================================================
// §3  CanCoexist — the isolated conflict predicate (v1 blunt body)
// ===========================================================================
//
// THE ONE function the smart-coexistence upgrade replaces. v1 body:
// two hooks on a target can coexist iff their signatures are compatible
// (share the thunk) AND neither is replace/around (those assume
// worst-case full footprint). When the smart work lands, this becomes
// footprint-overlap analysis per the spec §5; Add() and the chain
// container do not change.
bool CanCoexist(const Chain&                            chain,
                kcdx::hook_payload::Mode                incomingMode,
                const kcdx::hook_signature::Signature&  incomingSig,
                bool                                    incomingIsCallsite,
                std::string&                            whyNot) {
    using Mode = kcdx::hook_payload::Mode;

    // A callsite chain (keyed by the call-instruction VA) and a
    // function-entry chain (keyed by the callee VA) are distinct kinds of
    // interception; they must not share a chain even in the (vanishingly
    // unlikely) event their VAs coincide. Mediate by load order — the
    // earlier-installed kind owns the site; the later loses loudly. This
    // is the isolated callsite extension of the predicate (no cross-engine
    // knowledge; the decision stays inside hook_chain).
    if (chain.isCallsite != incomingIsCallsite) {
        whyNot = chain.isCallsite
            ? "this address is already redirected by a mode='callsite' "
              "hook; a function-entry hook cannot share it (load-order-"
              "loses; the callsite hook installed first wins)"
            : "this address already has a function-entry hook; a "
              "mode='callsite' hook cannot share it (load-order-loses; "
              "the function-entry hook installed first wins)";
        return false;
    }

    if (!SignaturesCompatibleImpl(chain.sig, incomingSig)) {
        whyNot = "target already hooked with an incompatible signature; "
                 "all hooks sharing a target must declare the same "
                 "argument + return types (v1 shares one marshaling "
                 "thunk per target)";
        return false;
    }

    // v1 worst-case: replace/around are assumed to touch everything, so
    // they cannot coexist with any other hook on the same target.
    const bool incomingExclusive =
        (incomingMode == Mode::Replace || incomingMode == Mode::Around);
    for (const auto& e : chain.entries) {
        const bool existingExclusive =
            (e.mode == Mode::Replace || e.mode == Mode::Around);
        if (incomingExclusive || existingExclusive) {
            // Engine entries are bootstrap targets — when the existing
            // exclusive entry is engine-owned (today: ModManager_ctor's
            // replace, the only engine replace), surface a teaching error
            // that names the bootstrap-target nature and points at the
            // docs. The wording is the canonical bootstrap-reject text
            // referenced by docs/cpp/hook.md § "Bootstrap targets" and
            // by the cap-NN-modmanager-reject regression rows; keep the
            // substring "engine bootstrap point" stable for greppability.
            if (e.isEngine && e.mode == Mode::Replace) {
                whyNot =
                    std::string(e.name) +
                    " is an engine bootstrap point with a replace "
                    "contract; cannot be additionally hooked. See "
                    "docs/cpp/hook.md \xc2\xa7 Bootstrap targets.";
                return false;
            }
            whyNot =
                std::string("target already has a '") +
                kcdx::hook_payload::ModeToken(e.mode) +
                "' hook; a '" + kcdx::hook_payload::ModeToken(incomingMode) +
                "' hook cannot coexist with replace/around on the same "
                "target in v1 (load-order-loses; smart footprint "
                "coexistence is future work)";
            return false;
        }
    }
    return true;
}

// ===========================================================================
// §4  Type-keyed slot <-> Lua marshaling (the wstr-correct path)
// ===========================================================================
//
// hook_chain owns its own marshaling, keyed on the parsed
// hook_signature::Type per slot — NOT the legacy type_info_t /
// value_wrapper_t (which collapses wstr and cstr into one "string" and
// reads wstr as a narrow char*, truncating UTF-16 at the first null).
// The author gets a BARE Lua value per param (no wrapper userdata), so
// they write `szApp:find(":")`, not `szApp:get():...`.
//
// Strings need a lifetime arena: when a callback returns a changed
// cstr/wstr, the native side reads the pointer AFTER the callback (and
// the lua_pcall) returns, so the bytes must outlive the Lua string the
// author produced. We pin converted strings in a per-dispatch arena
// cleared at the end of each top-level dispatch. Dispatch is
// main-thread-only (the engine marshals off-thread hits), so a
// single thread-local arena is safe.

// Per-dispatch string-pin arena. PinUtf8 keeps a UTF-8 (cstr) buffer;
// PinWide keeps a UTF-16 (wstr) buffer. Pointers returned stay valid
// until the OUTERMOST dispatch finishes.
//
// Re-entrancy: a hook is re-entered when an `around` callback's
// orig() (or any callback) calls into another hooked function — or the
// same one — before the first dispatch returns. That is ALLOWED (the
// author is trusted; a genuinely infinite hook->original->hook loop is
// the author's bug and surfaces as a natural stack overflow, like any
// runaway recursion). The engine's only job is to not corrupt itself
// while it happens: the pin arena is shared thread-local state, so an
// inner dispatch must NOT free the outer dispatch's pinned strings. We
// track nesting depth and clear the arena only when the outermost
// dispatch (depth 0) exits. The depth is NOT a limiter — it exists
// solely to time the arena clear correctly. Dispatch is main-thread-
// only (the engine marshals off-thread hits), so a thread-local
// counter is sufficient.
thread_local std::vector<std::unique_ptr<std::string>>  g_pinUtf8;
thread_local std::vector<std::unique_ptr<std::wstring>> g_pinWide;
thread_local int g_dispatchDepth = 0;

void ClearPinArena() { g_pinUtf8.clear(); g_pinWide.clear(); }

const char* PinUtf8(std::string s) {
    g_pinUtf8.emplace_back(std::make_unique<std::string>(std::move(s)));
    return g_pinUtf8.back()->c_str();
}
const wchar_t* PinWide(std::wstring s) {
    g_pinWide.emplace_back(std::make_unique<std::wstring>(std::move(s)));
    return g_pinWide.back()->c_str();
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}
std::wstring Utf8ToWide(const char* s) {
    if (!s) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

// Push the value at `slot` (a uintptr_t-sized cell) onto the Lua stack
// as a bare value, per its hook_signature::Type. Stack effect: +1.
void PushSlot(lua_State* L, const uintptr_t* slot,
              kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Bool: lua_pushboolean(L, (*slot != 0) ? 1 : 0); break;
        case T::F32:  lua_pushnumber(L, (lua_Number)*(const float*)slot);  break;
        case T::F64:  lua_pushnumber(L, (lua_Number)*(const double*)slot); break;
        case T::Ptr:
            // Pointers go through a kcdx.memory.pointer userdata, never
            // lua_pushinteger (LUA_NUMBER=float loses pointer magnitude
            // beyond 2^24).
            kcdx::lua_bind_helpers::PushPointer(
                L, kcdx::lua_memory::pointer((uintptr_t)*slot));
            break;
        case T::Cstr: {
            const char* p = *(const char* const*)slot;
            if (p) lua_pushstring(L, p); else lua_pushnil(L);
            break;
        }
        case T::Wstr: {
            const wchar_t* p = *(const wchar_t* const*)slot;
            if (p) {
                std::string u8 = WideToUtf8(p);
                lua_pushlstring(L, u8.data(), u8.size());
            } else {
                lua_pushnil(L);
            }
            break;
        }
        case T::Void: lua_pushnil(L); break;
        // All integer widths/signs: push as Lua integer. 64-bit values
        // above 2^53 lose precision through lua_Number — documented
        // limit; pointer-magnitude integers should use ptr instead.
        default:      lua_pushinteger(L, (lua_Integer)(int64_t)*slot); break;
    }
}

// Read the Lua value at `idx` and write it into `slot` per its type.
// For strings, the converted bytes are pinned in the per-dispatch arena
// so they outlive the call. Leaves slot unchanged if the Lua value is
// the wrong type (a callback that returns nil for a slot keeps the
// original — see the mutate-by-return contract).
void WriteSlot(lua_State* L, int idx, uintptr_t* slot,
               kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Bool:
            if (!lua_isnil(L, idx)) *slot = lua_toboolean(L, idx) ? 1 : 0;
            break;
        case T::F32:
            if (lua_isnumber(L, idx)) *(float*)slot = (float)lua_tonumber(L, idx);
            break;
        case T::F64:
            if (lua_isnumber(L, idx)) *(double*)slot = (double)lua_tonumber(L, idx);
            break;
        case T::Ptr:
            // Accept a kcdx.memory.pointer userdata or an integer VA.
            // Non-throwing: a wrong-typed return for a ptr slot leaves the
            // slot unchanged (mutate-by-return: unspecified return = keep).
            if (lua_isuserdata(L, idx)) {
                if (lua_getmetatable(L, idx)) {
                    luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
                    const bool match = lua_rawequal(L, -1, -2) != 0;
                    lua_pop(L, 2);
                    if (match) {
                        auto* p = static_cast<kcdx::lua_memory::pointer*>(
                            lua_touserdata(L, idx));
                        if (p) *slot = (uintptr_t)p->get_address();
                    }
                }
            } else if (lua_isnumber(L, idx)) {
                *slot = (uintptr_t)lua_tointeger(L, idx);
            }
            break;
        case T::Cstr:
            if (lua_isstring(L, idx)) {
                *(const char**)slot = PinUtf8(std::string(lua_tostring(L, idx)));
            }
            break;
        case T::Wstr:
            if (lua_isstring(L, idx)) {
                *(const wchar_t**)slot = PinWide(Utf8ToWide(lua_tostring(L, idx)));
            }
            break;
        case T::Void: break;
        default:
            if (lua_isnumber(L, idx)) *slot = (uintptr_t)(int64_t)lua_tointeger(L, idx);
            break;
    }
}

// Push the target's params as positional Lua call arguments. Returns the
// count pushed (== param count).
int PushParamsPositional(lua_State* L, const Chain& chain,
                         const kcdx::rom::runtime_func_t::parameters_t* params) {
    const int n = static_cast<int>(chain.sig.args.size());
    for (int i = 0; i < n; ++i) {
        const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(
            params->get_arg_ptr(static_cast<uint8_t>(i)));
        PushSlot(L, slot, chain.sig.args[i].type);
    }
    return n;
}

// Write Lua return values (stack indices firstRet..) back into the param
// slots — the mutate-by-return contract for `before`. retCount==0 leaves
// args untouched; returned values replace args 0..retCount-1.
void WriteBackParams(lua_State* L, const Chain& chain,
                     kcdx::rom::runtime_func_t::parameters_t* params,
                     int firstRet, int retCount) {
    const int n = static_cast<int>(chain.sig.args.size());
    for (int i = 0; i < n && i < retCount; ++i) {
        uintptr_t* slot = reinterpret_cast<uintptr_t*>(
            params->get_arg_ptr(static_cast<uint8_t>(i)));
        WriteSlot(L, firstRet + i, slot, chain.sig.args[i].type);
    }
}

// Push the current return value onto the Lua stack (for `after`). +1.
void PushReturn(lua_State* L, const Chain& chain,
                kcdx::rom::runtime_func_t::return_value_t* rv) {
    PushSlot(L, reinterpret_cast<const uintptr_t*>(rv->get()),
             chain.sig.returnType);
}

// Write a Lua value at `idx` into the return slot.
void WriteReturn(lua_State* L, int idx, const Chain& chain,
                 kcdx::rom::runtime_func_t::return_value_t* rv) {
    WriteSlot(L, idx, reinterpret_cast<uintptr_t*>(rv->get()),
              chain.sig.returnType);
}

// ===========================================================================
// §5b  mid-hook capture handles + skip flag
// ===========================================================================
//
// A mid hook captures register/memory values at one instruction inside a
// function. The author's callback receives a table of capture HANDLES —
// one per capture — each a small userdata with :get() / :set(). :get()
// reads the captured value (as a Lua number/pointer); :set(v) writes it
// back into the JIT capture slot, which make_jit_midfunc's unconditional
// "apply change" loop then stores into the real register/memory after the
// callback returns (runtime_func_t.cpp:608+).
//
// Slots live in the JIT trampoline's STACK payload for the duration of
// the dispatch only. A handle is valid ONLY inside the callback; stashing
// one and using it later reads freed stack (author bug — same hazard as
// retaining any by-reference callback argument).
//
// The capture payload uses a 16-BYTE slot stride (the JIT writes
// [rsp + 16*i]) — NOT parameters_t::get_arg_ptr's 8-byte stride. We index
// the payload base directly. (cap-04 has one capture so slot0 coincides
// in both strides; the mismatch only bites 2+ captures — see
// project_kcdx_phase2b_hook_restructure memory.)

// Skip-original flag: a single byte make_jit_midfunc reads (Auto mode)
// after MidDispatch returns. Non-zero => the captured instruction is
// skipped (resume past it). hook_chain owns its OWN flag (not
// scripting::g_mid_skip_original) so it stays self-contained for the
// eventual legacy-scripting discard. Main-thread-only dispatch
// (the engine marshals off-thread hits) means a plain byte suffices;
// MidDispatch clears it at entry and sets it from the callback's return.
uint8_t g_midSkipOriginal = 0;

// One capture handle: a pointer into the live JIT slot payload + the
// capture's type string. Bare value (not a kcdx.memory.pointer) — the
// author reads/writes plain numbers off a disassembler.
struct CaptureHandle {
    void*       slot = nullptr;  // (char*)&params->m_arguments + 16*i
    const char* type = "i64";    // capExprs[i]'s parsed type
};

const char* const kCaptureHandleMetatable = "kcdx.hook.capture";

// Push the slot value as a bare Lua value, per the capture type string.
// f32/f64 -> number; ptr -> kcdx.memory.pointer userdata (exact, since
// LUA_NUMBER is float); integer widths -> Lua integer (read at the slot's
// width so the upper bits of a 16-byte slot don't leak in).
void PushCaptureValue(lua_State* L, const CaptureHandle* h) {
    const std::string t = h->type;
    if (t == "f32")        lua_pushnumber(L, (lua_Number)*(const float*)h->slot);
    else if (t == "f64" || t == "double")
                           lua_pushnumber(L, (lua_Number)*(const double*)h->slot);
    else if (t == "float") lua_pushnumber(L, (lua_Number)*(const float*)h->slot);
    else if (t == "bool")  lua_pushboolean(L, (*(const uint64_t*)h->slot != 0) ? 1 : 0);
    else if (t == "ptr")
        kcdx::lua_bind_helpers::PushPointer(
            L, kcdx::lua_memory::pointer(*(const uintptr_t*)h->slot));
    else if (t == "i8")    lua_pushinteger(L, (lua_Integer)*(const int8_t*)h->slot);
    else if (t == "u8")    lua_pushinteger(L, (lua_Integer)*(const uint8_t*)h->slot);
    else if (t == "i16")   lua_pushinteger(L, (lua_Integer)*(const int16_t*)h->slot);
    else if (t == "u16")   lua_pushinteger(L, (lua_Integer)*(const uint16_t*)h->slot);
    else if (t == "i32")   lua_pushinteger(L, (lua_Integer)*(const int32_t*)h->slot);
    else if (t == "u32")   lua_pushinteger(L, (lua_Integer)*(const uint32_t*)h->slot);
    else if (t == "u64")   lua_pushinteger(L, (lua_Integer)*(const uint64_t*)h->slot);
    else                   lua_pushinteger(L, (lua_Integer)*(const int64_t*)h->slot);  // i64 default
}

// Write a Lua value at `idx` into the slot, per the capture type. Widens
// to the full 16-byte slot for integer types (zero/sign-extend the rest)
// so the JIT's 8-byte reload reads a clean value. Wrong-typed value is a
// no-op (mutate-by-call: not calling :set() leaves the captured value).
void WriteCaptureValue(lua_State* L, int idx, const CaptureHandle* h) {
    const std::string t = h->type;
    if (t == "f32" || t == "float") {
        if (lua_isnumber(L, idx)) *(float*)h->slot = (float)lua_tonumber(L, idx);
    } else if (t == "f64" || t == "double") {
        if (lua_isnumber(L, idx)) *(double*)h->slot = (double)lua_tonumber(L, idx);
    } else if (t == "bool") {
        if (!lua_isnil(L, idx)) *(uint64_t*)h->slot = lua_toboolean(L, idx) ? 1 : 0;
    } else if (t == "ptr") {
        // Accept a kcdx.memory.pointer userdata or an integer VA.
        if (lua_isuserdata(L, idx)) {
            if (lua_getmetatable(L, idx)) {
                luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
                const bool match = lua_rawequal(L, -1, -2) != 0;
                lua_pop(L, 2);
                if (match) {
                    auto* p = static_cast<kcdx::lua_memory::pointer*>(
                        lua_touserdata(L, idx));
                    if (p) *(uintptr_t*)h->slot = (uintptr_t)p->get_address();
                }
            }
        } else if (lua_isnumber(L, idx)) {
            *(uintptr_t*)h->slot = (uintptr_t)lua_tointeger(L, idx);
        }
    } else {
        // Integer widths: store full 64 bits so the slot is clean for the
        // JIT reload. The author's narrower intent (i8/i32) is honored on
        // the read side; on write we sign-extend through int64.
        if (lua_isnumber(L, idx))
            *(uint64_t*)h->slot = (uint64_t)(int64_t)lua_tointeger(L, idx);
    }
}

int CaptureHandle_get(lua_State* L) {
    auto* h = static_cast<CaptureHandle*>(
        luaL_checkudata(L, 1, kCaptureHandleMetatable));
    PushCaptureValue(L, h);
    return 1;
}

int CaptureHandle_set(lua_State* L) {
    auto* h = static_cast<CaptureHandle*>(
        luaL_checkudata(L, 1, kCaptureHandleMetatable));
    WriteCaptureValue(L, 2, h);
    return 0;
}

// Lazily create the capture-handle metatable on the live state. Raw Lua C
// API only (no kcdx-side static-const sentinel on the shared lua_State).
void EnsureCaptureHandleMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kCaptureHandleMetatable)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");  // mt.__index = mt
        lua_pushcfunction(L, CaptureHandle_get);
        lua_setfield(L, -2, "get");
        lua_pushcfunction(L, CaptureHandle_set);
        lua_setfield(L, -2, "set");
    }
    lua_pop(L, 1);
}

// Push a fresh capture handle userdata for slot i of the dispatch payload.
void PushCaptureHandle(lua_State* L, void* slot, const char* type) {
    auto* h = static_cast<CaptureHandle*>(
        lua_newuserdata(L, sizeof(CaptureHandle)));
    h->slot = slot;
    h->type = type;
    luaL_getmetatable(L, kCaptureHandleMetatable);
    lua_setmetatable(L, -2);
}

// ===========================================================================
// §6  DispatchPre / DispatchPost — the C callbacks the JIT thunk calls
// ===========================================================================

// Resolve the Chain for a target under the lock, then release it before
// running Lua (lua_pcall can run arbitrary code; we don't hold the mutex
// across it). Returns nullptr if no chain (shouldn't happen for an
// installed target).
Chain* ResolveChainForDispatch(uintptr_t target) {
    std::lock_guard<std::mutex> lock(g_chainsMu);
    return FindChain(target);
}

// Handle a replace or around entry (both own the original-call
// decision; both fire in the pre-phase). For replace: run the callback
// with positional params, write its return into return_value, original
// never runs. For around: run the callback with (call_original, params...)
// — the callback decides whether/when to invoke the original — and write
// its return into return_value.
void DispatchExclusive(lua_State* L, Chain& chain, const ChainEntry& e,
                       const kcdx::rom::runtime_func_t::parameters_t* params,
                       kcdx::rom::runtime_func_t::return_value_t* return_value) {
    using Mode = kcdx::hook_payload::Mode;
    const bool hasReturn =
        (chain.sig.returnType != kcdx::hook_signature::Type::Void);

    // C-kind branch: real dispatch via the BuildCDispatchThunk-emitted
    // trampoline (chunks 3+4). Around takes (params, rv, callOriginalCThunk);
    // Replace takes (params, rv). Both write into rv per the per-mode codegen;
    // the thunk handles all typed marshaling.
    if (e.kind == ChainEntry::Kind::C) {
        if (!e.cDispatchThunk) {
            log::WarnF("hook_chain: C-kind exclusive entry '%s' (plugin '%s') "
                       "has no cDispatchThunk — skipping (BuildCDispatchThunk "
                       "failed at AddC time)",
                       e.name.c_str(), e.pluginName.c_str());
            return;
        }
        if (e.mode == Mode::Around) {
            using Thunk = void (*)(
                const kcdx::rom::runtime_func_t::parameters_t*,
                kcdx::rom::runtime_func_t::return_value_t*,
                void*);
            reinterpret_cast<Thunk>(e.cDispatchThunk)(
                params, return_value,
                reinterpret_cast<void*>(chain.callOriginalCThunk));
        } else {  // Replace
            using Thunk = void (*)(
                const kcdx::rom::runtime_func_t::parameters_t*,
                kcdx::rom::runtime_func_t::return_value_t*);
            reinterpret_cast<Thunk>(e.cDispatchThunk)(params, return_value);
        }
        (void)hasReturn;  // rv writeback handled by the thunk
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        // #3 — exclusive (replace/around) dead callback ref. The caller has
        // ALREADY set runOriginal=false for this entry, so the original is
        // suppressed AND this callback is skipped: the hooked game function is
        // now a default-return no-op. That is a NEUTRALIZED live function —
        // Error severity, NOT a quiet skip. Error-once per entry (handleId) so
        // a per-frame target does not flood while the author still sees the one
        // loud line. callbackRef==-2 (LUA_NOREF, uninstalled) never reaches
        // DispatchExclusive (the entry would not be in the exclusive branch),
        // so any non-function here is the genuine-lost-ref case.
        if (DeadRefFirstObservation(e.handleId)) {
            log::ErrorF(
                "hook_chain: %s '%s' (plugin '%s') at 0x%p — callback ref "
                "invalid (closure GC'd or never set); original SUPPRESSED and "
                "callback SKIPPED, so the hooked function now returns a "
                "default/garbage value (neutralized). Error-once per entry.",
                kcdx::hook_payload::ModeToken(e.mode), e.name.c_str(),
                e.pluginName.c_str(), (void*)chain.targetVa);
        }
        return;
    }
    const int top0 = lua_gettop(L);  // [..., fn]

    int nargs = 0;
    if (e.mode == Mode::Around) {
        // First param is `orig` — the call_original thunk, itself a
        // lua_CFunction (a typed call over MinHook's pOriginal trampoline,
        // JIT'd at install). Pushing it directly makes orig(args...) a
        // normal Lua call: it marshals, runs the original, returns the
        // typed result. If the thunk is missing (build failed), push a
        // closure that errors clearly rather than a nil the author would
        // call and crash on.
        if (chain.callOriginalThunk) {
            lua_pushcfunction(L,
                reinterpret_cast<lua_CFunction>(chain.callOriginalThunk));
        } else {
            lua_pushcfunction(L, [](lua_State* Ls) -> int {
                return luaL_error(Ls, "call_original: unavailable (the "
                    "engine failed to build the call-through thunk for "
                    "this target; see kcdx.log)");
            });
        }
        ++nargs;
    }
    nargs += PushParamsPositional(L, chain,
        const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params));

    const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("hook_chain: %s '%s' (plugin '%s') threw: %s",
                    kcdx::hook_payload::ModeToken(e.mode), e.name.c_str(),
                    e.pluginName.c_str(), msg ? msg : "<no message>");
        lua_settop(L, top0 - 1);
        return;
    }
    const int retCount = lua_gettop(L) - (top0 - 1);
    if (hasReturn && retCount > 0) {
        WriteReturn(L, top0, chain, return_value);
    }
    lua_settop(L, top0 - 1);  // balance
}

// pre callback: runs before the original. Returns false to suppress the
// original (replace/around that didn't call it). before/around/replace
// all fire here in load order.
bool DispatchPre(const kcdx::rom::runtime_func_t::parameters_t* params,
                 const uint8_t  /*param_count*/,
                 kcdx::rom::runtime_func_t::return_value_t* return_value,
                 const uintptr_t target_func_ptr) {
    // Enter a dispatch level FIRST — unconditionally, before any early
    // return — so it pairs exactly with DispatchPost's unconditional
    // decrement (the JIT always invokes post after pre). Spans
    // pre -> original -> post. A re-entrant dispatch (an around's orig()
    // calling another hooked fn) nests fully inside this one; that is
    // allowed. The breadcrumb makes a runaway hook->original->hook loop
    // traceable in the dev log before the stack overflows (depth climbs
    // each line) — it is NOT a limiter; a non-terminating loop is the
    // hook author's bug, same as any runaway recursion.
    ++g_dispatchDepth;
    if (g_dispatchDepth > 1) {
        log::DebugF("hook_chain: re-entrant dispatch depth=%d at 0x%p "
                    "(allowed; a non-terminating loop here is the hook "
                    "author's bug)",
                    g_dispatchDepth, (void*)target_func_ptr);
    }

    lua_State* L = g_L;
    Chain* chain = ResolveChainForDispatch(target_func_ptr);
    if (!chain || chain->entries.empty()) return true;

    // Fire breadcrumb: record that the game just executed THIS detour, naming
    // the chain's representative owner (the first entry — load-order-first,
    // the one that fixed the thunk). One record per chokepoint (DispatchPre and
    // DispatchPost each record once for a non-empty chain), not per chain
    // entry — keeps the 32-slot ring from being flooded by a many-hook chain
    // and answers "which detour did the game last run" — the missing link for
    // the 0xC8 crash. ALWAYS-ON,
    // zero-allocation, no-log: a relaxed atomic bump + four stores
    // (the on-thread fast path stays zero-allocation). The
    // borrowed name pointers are process-lifetime (Chains never destroyed).
    modification_inventory::RecordFire(
        target_func_ptr,
        chain->entries.front().pluginName.c_str(),
        chain->entries.front().name.c_str());

    // On the C-only path (no Lua callbacks ever installed on this
    // target), a missing L is fine: C dispatch does not touch the Lua
    // VM. We branch per-entry below: Lua entries early-skip when L is
    // null; C entries run regardless. (The Lua-only path keeps the
    // pre-existing return-true-default-runOriginal behavior.)

    const bool onMainThread = log::IsGameMainThread();
    bool runOriginal = true;

    for (const ChainEntry& e : chain->entries) {
        using Mode = kcdx::hook_payload::Mode;
        if (e.mode == Mode::After) continue;  // after fires in DispatchPost

        // Off-thread routing branch. The engine marshals off-thread hits;
        // in v1 Marshal (0) / Skip (1) / Error (2) all skip the callback;
        // Marshal degrades
        // to Skip-with-warn-once (no off-thread sites
        // observed in the cap-15..22 + cap-35 corpus — a real arg-
        // snapshot marshal is its own future cycle when this warn ever
        // fires). For replace/around we explicitly leave runOriginal
        // alone: when the C/Lua callback is skipped, the original
        // function runs normally (its pre-hook default behavior).
        //
        // Engine-stamped C-kind carve-out: an entry with isEngine=true +
        // kind==C bypasses the off-thread filter. AP6 (no Lua callback
        // off-thread) doesn't apply because the dispatch path contains no
        // Lua callback — engine-stamped C entries are kcdx-internal C
        // functions registered via AddCEngine (the engine team owns and
        // audits them; plugins cannot stamp the engine identity). The
        // bypass exists because the dispatcher's main-thread classifier
        // depends on hook_chain::SetLuaState having run, and SetLuaState's
        // trigger path IS an engine C-Before callback (the lua_pcall
        // L-capture, src/hooks.cpp:HookedLuaPcall_Engine): gating that
        // callback on the classifier creates a self-perpetuating dead-
        // classifier deadlock. See .claude/rules/lua-callback-threading.md
        // §Engine bootstrap carve-out for the three-hop loop the bypass
        // breaks; see PROBE α in docs/known-issues/cap-59-fires...md for
        // the observed evidence.
        const bool isEngineCBypass =
            e.isEngine && e.kind == ChainEntry::Kind::C;
        if (!onMainThread && !isEngineCBypass) {
            OffThreadShouldSkip(e.offThread, e.handleId, "pre",
                                target_func_ptr,
                                e.name.c_str(), e.pluginName.c_str());
            continue;
        }

        // C-kind branch: real dispatch via the BuildCDispatchThunk-emitted
        // trampoline (chunks 3+4). Before is non-exclusive (engine still
        // runs the original); Around / Replace are exclusive and flow
        // through DispatchExclusive (which also covers the C path).
        if (e.kind == ChainEntry::Kind::C) {
            if (e.mode == Mode::Before) {
                if (!e.cDispatchThunk) {
                    log::WarnF("hook_chain: C-kind Before '%s' (plugin '%s') "
                               "has no cDispatchThunk — skipping",
                               e.name.c_str(), e.pluginName.c_str());
                    continue;
                }
                using Thunk = void (*)(
                    kcdx::rom::runtime_func_t::parameters_t*);
                reinterpret_cast<Thunk>(e.cDispatchThunk)(
                    const_cast<kcdx::rom::runtime_func_t::parameters_t*>(
                        params));
            } else if (e.mode == Mode::Around || e.mode == Mode::Replace) {
                runOriginal = false;
                DispatchExclusive(L, *chain, e, params, return_value);
            }
            continue;
        }

        // Lua-kind branch (the original path).
        if (!L) continue;  // VM not bound yet — skip the Lua callback safely
        // Push the callback closure.
        lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            // #13 — DispatchPre dead callback ref. This guard precedes the
            // exclusive dispatch below, so a replace/around with a lost ref is
            // caught HERE (continue) and runOriginal stays true: the original
            // runs un-hooked. A before entry simply does not fire. Either way
            // this entry is INERT, not the function-neutralizing #3 case — so
            // Warn (a degradation), warn-once per entry (handleId) to honor the
            // per-frame hot-path contract. Only the genuine-lost-ref case
            // reaches here: callbackRef==-2 (LUA_NOREF, uninstalled) would have
            // produced a non-function too, but an uninstalled entry is removed
            // from the chain (Uninstall erases it), so a live entry in this
            // loop with a non-function ref is a real lost ref, not the
            // sentinel. (The -2 sentinel is checked explicitly in MidDispatch
            // where mid lives on the Chain and is not erased.)
            if (DeadRefFirstObservation(e.handleId)) {
                log::WarnF(
                    "hook_chain: %s '%s' (plugin '%s') at 0x%p — callback ref "
                    "no longer a function (registry ref lost); entry inert — it "
                    "will not fire (the original runs un-hooked). Warn-once per "
                    "entry.",
                    kcdx::hook_payload::ModeToken(e.mode), e.name.c_str(),
                    e.pluginName.c_str(), (void*)target_func_ptr);
            }
            continue;
        }

        if (e.mode == Mode::Before) {
            // before(self, szApp, ...) -> [changed args...] | nothing
            const int top0 = lua_gettop(L);  // stack: [..., fn]
            const int nargs = PushParamsPositional(L, *chain,
                const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params));
            const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
            if (status != 0) {
                const char* msg = lua_tostring(L, -1);
                log::ErrorF("hook_chain: before '%s' (plugin '%s') on "
                            "0x%p threw: %s", e.name.c_str(),
                            e.pluginName.c_str(), (void*)target_func_ptr,
                            msg ? msg : "<no message>");
                lua_pop(L, 1);
                continue;
            }
            // Returns are at top0 .. lua_gettop(L). top0 was the fn slot,
            // which pcall popped; results start where fn was (top0).
            const int retCount = lua_gettop(L) - (top0 - 1);
            if (retCount > 0) {
                WriteBackParams(L, *chain,
                    const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params),
                    top0, retCount);
            }
            lua_settop(L, top0 - 1);  // pop all results; balance stack
            // before NEVER suppresses the original.
        } else if (e.mode == Mode::Replace || e.mode == Mode::Around) {
            // replace/around own the original-call decision; the thunk
            // must NOT auto-run the original after we return.
            runOriginal = false;
            DispatchExclusive(L, *chain, e, params, return_value);
        }
    }
    return runOriginal;
}

// post callback: runs after the original. Only `after` entries fire
// here; each receives the current return value and may return a
// replacement.
void DispatchPost(const kcdx::rom::runtime_func_t::parameters_t* params,
                  const uint8_t /*param_count*/,
                  kcdx::rom::runtime_func_t::return_value_t* return_value,
                  const uintptr_t target_func_ptr) {
    lua_State* L = g_L;
    // Resolve the chain regardless of L — a C-only chain (no Lua entries
    // installed on this target via kcdx.hook from Lua) must still
    // dispatch its C After entries even before the VM is bound. Lua
    // After entries inside the loop still gate on L below.
    Chain* chain = ResolveChainForDispatch(target_func_ptr);

    const bool hasReturn = chain &&
        (chain->sig.returnType != kcdx::hook_signature::Type::Void);

    const bool onMainThread = log::IsGameMainThread();

    if (chain && !chain->entries.empty()) {

    // Fire breadcrumb (mirror of DispatchPre's). The post chokepoint records
    // too: a crash AFTER the original returns (the 0xC8 shape — fault is
    // post-original, in the asset/shader window) leaves the last breadcrumb
    // at the Post fire, naming the detour the game most recently completed.
    // Same always-on, zero-allocation, no-log hot-path contract.
    modification_inventory::RecordFire(
        target_func_ptr,
        chain->entries.front().pluginName.c_str(),
        chain->entries.front().name.c_str());

    for (const ChainEntry& e : chain->entries) {
        if (e.mode != kcdx::hook_payload::Mode::After) continue;

        // Off-thread routing branch (mirror of DispatchPre's). After
        // entries fire in Post; same per-entry policy + same dedup
        // keying as Pre. Engine-stamped C-kind carve-out mirrors
        // DispatchPre's — same predicate, same justification (see the
        // comment block at DispatchPre's gate above).
        const bool isEngineCBypass =
            e.isEngine && e.kind == ChainEntry::Kind::C;
        if (!onMainThread && !isEngineCBypass) {
            OffThreadShouldSkip(e.offThread, e.handleId, "post",
                                target_func_ptr,
                                e.name.c_str(), e.pluginName.c_str());
            continue;
        }

        // C-kind branch: real dispatch via the After-mode thunk
        // (chunks 3+4). The thunk handles void-vs-non-void return per
        // D-c-fn-abi-3 — void returns ignore rv; non-void returns
        // read origReturn from rv pre-call + write the typed return
        // back to rv post-call.
        if (e.kind == ChainEntry::Kind::C) {
            if (!e.cDispatchThunk) {
                log::WarnF("hook_chain: C-kind After '%s' (plugin '%s') has "
                           "no cDispatchThunk — skipping",
                           e.name.c_str(), e.pluginName.c_str());
                continue;
            }
            using Thunk = void (*)(
                const kcdx::rom::runtime_func_t::parameters_t*,
                kcdx::rom::runtime_func_t::return_value_t*);
            reinterpret_cast<Thunk>(e.cDispatchThunk)(params, return_value);
            (void)hasReturn;
            continue;
        }

        if (!L) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            // #13 — DispatchPost dead callback ref. Only `after` entries reach
            // here. A lost ref makes this after-hook inert: the return value
            // passes through un-transformed. A degradation, not a crash-risk —
            // Warn, warn-once per entry (handleId), hot-path-safe (the live
            // path never reaches this block). Genuine-lost-ref only: an
            // uninstalled entry is erased from the chain, so a live non-function
            // ref here is a real lost ref, not the -2 sentinel.
            if (DeadRefFirstObservation(e.handleId)) {
                log::WarnF(
                    "hook_chain: after '%s' (plugin '%s') at 0x%p — callback "
                    "ref no longer a function (registry ref lost); entry inert "
                    "— it will not fire (the return value passes through "
                    "un-transformed). Warn-once per entry.",
                    e.name.c_str(), e.pluginName.c_str(),
                    (void*)target_func_ptr);
            }
            continue;
        }

        const int top0 = lua_gettop(L);  // [..., fn]
        int nargs = 0;
        if (hasReturn) {
            PushReturn(L, *chain, return_value);
            nargs = 1;
        }
        const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
        if (status != 0) {
            const char* msg = lua_tostring(L, -1);
            log::ErrorF("hook_chain: after '%s' (plugin '%s') on 0x%p "
                        "threw: %s", e.name.c_str(), e.pluginName.c_str(),
                        (void*)target_func_ptr, msg ? msg : "<no message>");
            lua_pop(L, 1);
            continue;
        }
        const int retCount = lua_gettop(L) - (top0 - 1);
        if (hasReturn && retCount > 0) {
            // after returns the (possibly changed) return value.
            WriteReturn(L, top0, *chain, return_value);
        }
        lua_settop(L, top0 - 1);  // balance
    }
    }  // if (chain && !entries.empty())

    // Leave this dispatch level. DispatchPost is the matching exit for
    // the DispatchPre increment (the JIT always invokes post, even when
    // pre returned false to skip the original). The decrement runs
    // UNCONDITIONALLY — outside the chain guard above — so depth stays
    // balanced even on the no-chain / no-after path. Only the OUTERMOST
    // dispatch (depth back to 0) frees the pinned strings, so a nested
    // re-entrant dispatch can't pull the arena out from under an outer
    // dispatch whose original is still reading a pinned arg.
    if (g_dispatchDepth > 0) --g_dispatchDepth;
    if (g_dispatchDepth == 0) {
        ClearPinArena();
    }
}

// ===========================================================================
// §6b  MidDispatch — the C callback make_jit_midfunc invokes
// ===========================================================================
//
// Runs once per call to the captured instruction. Builds a table of
// capture handles (keyed by name for the name-map form, or 1..N for the
// positional list form), runs the author's `mid` callback with it, and
// decides run-vs-skip from the RETURN value:
//   return "skip" or true  -> set g_midSkipOriginal (instruction skipped)
//   return nothing/false    -> leave it clear (instruction runs)
// :set() calls on handles mutate the JIT slots in place; the JIT's
// "apply change" loop stores them back into the real reg/mem afterwards.
//
// This is the FRESH dispatcher (not the legacy scripting::dynamic_hook_mid
// which carried the cap-04-c bug: it dup'd the args table with a fragile
// lua_insert/lua_pushvalue juggle to read `args._skip` post-pcall). Here
// the decision rides the return value — no global args._skip, no juggle.
//
// Returns 0: the JIT no longer reads rax (resume is decided by the skip
// flag in Auto mode).
uintptr_t MidDispatch(const kcdx::rom::runtime_func_t::parameters_t* params,
                      const size_t  param_count,
                      const uintptr_t target_func_ptr) {
    // Clear the skip flag at entry — start from a known state so a stale
    // "set" from a previous mid dispatch can't carry over.
    g_midSkipOriginal = 0;

    Chain* chain = ResolveChainForDispatch(target_func_ptr);
    if (!chain || !chain->isMid) return 0;

    // Fire breadcrumb (mirror of DispatchPre/Post). A mid chain owns its
    // single callback on the Chain itself (midPluginName / midName), so the
    // owner comes from there. Same always-on, zero-allocation, no-log
    // hot-path contract (on-thread fast path stays zero-allocation).
    modification_inventory::RecordFire(target_func_ptr,
                                       chain->midPluginName.c_str(),
                                       chain->midName.c_str());

    // Off-thread routing branch (parallel to DispatchPre / DispatchPost).
    // Mid is one-per-VA in v1 so the dedup key is targetVa, not a per-
    // entry handleId. The same Marshal-degraded-to-Skip + Skip + Error
    // policy applies; skip leaves g_midSkipOriginal clear so the JIT
    // runs the captured instruction (the original behavior pre-hook).
    // Engine-stamped C-kind carve-out: mirror of DispatchPre/Post's. Mid
    // is one-per-VA so the engine flag lives on Chain (isMidEngine) not
    // on a per-entry ChainEntry::isEngine. Same predicate shape (engine
    // + C); same AP6-doesn't-apply justification. No engine mid sites
    // exist today; carve-out lands now for parity so a future engine
    // mid site cannot reintroduce the chicken-and-egg.
    const bool isEngineCBypassMid =
        chain->isMidEngine && chain->midKind == ChainEntry::Kind::C;
    if (!log::IsGameMainThread() && !isEngineCBypassMid) {
        OffThreadShouldSkip(chain->midOffThread, chain->targetVa, "mid",
                            target_func_ptr,
                            chain->midName.c_str(),
                            chain->midPluginName.c_str());
        return 0;
    }

    // C-kind mid branch: real dispatch via the Mid-mode thunk
    // (chunks 3+4). The thunk packs the JIT slot payload into a
    // stack-allocated kcdxHookCaptureValue[count] typed per
    // chain.capTypes[i], invokes the author cFn(values, count), reads the
    // typed values back into the slots post-call, and RETURNS the author's
    // kcdxMidResult (0 = run, nonzero = skip — the v2 ABI). We set
    // g_midSkipOriginal from that return here, exactly as the Lua branch
    // below sets it from the Lua callback's return — the C++ parity mirror
    // of the Lua mid `return "skip"`.
    if (chain->midKind == ChainEntry::Kind::C) {
        if (!chain->midCDispatchThunk) {
            log::WarnF("hook_chain: C-kind mid '%s' (plugin '%s') has no "
                       "midCDispatchThunk — skipping",
                       chain->midName.c_str(), chain->midPluginName.c_str());
            return 0;
        }
        // Build parallel-vector arrays of c_str() pointers so the thunk
        // (which receives them as `const char* const*`) can index them
        // by capture slot without dereferencing std::string.
        std::vector<const char*> capNames;
        std::vector<const char*> capTypes;
        capNames.reserve(chain->capNames.size());
        capTypes.reserve(chain->capTypes.size());
        for (const auto& s : chain->capNames) capNames.push_back(s.c_str());
        for (const auto& s : chain->capTypes) capTypes.push_back(s.c_str());
        const int count = static_cast<int>(
            (param_count < chain->capTypes.size())
                ? param_count : chain->capTypes.size());
        void* payload = reinterpret_cast<void*>(
            const_cast<uintptr_t*>(&params->m_arguments));
        using Thunk = int (*)(void*, int, const char* const*,
                              const char* const*);
        const int midResult = reinterpret_cast<Thunk>(chain->midCDispatchThunk)(
            payload, count,
            capNames.empty() ? nullptr : capNames.data(),
            capTypes.empty() ? nullptr : capTypes.data());
        if (midResult != 0) g_midSkipOriginal = 1;  // kcdxMidResult_Skip
        LOG_DEBUG_KV("MID_HOOK", "hook_chain.mid_dispatch_c",
            log::KV("target",        (void*)target_func_ptr),
            log::KV("captures",      (int64_t)count),
            log::KV("skip_original", (int64_t)g_midSkipOriginal));
        return 0;
    }

    lua_State* L = g_L;
    if (!L) return 0;
    // -2 (LUA_NOREF) is the DELIBERATE uninstalled / never-set sentinel — a mid
    // chain whose callback was uninstalled keeps the detour as a no-op shim
    // (mid is one-per-VA; the Chain is not erased). This path MUST stay silent
    // (#13): it is not a lost ref, it is "nothing installed here." The dead-ref
    // warn below fires ONLY for midCallbackRef >= 0 that yields a non-function.
    if (chain->midCallbackRef == -2) return 0;

    // Capture payload base. Slots are at 16-byte stride.
    char* payload = reinterpret_cast<char*>(
        const_cast<uintptr_t*>(&params->m_arguments));
    const size_t n = (param_count < chain->capTypes.size())
                         ? param_count : chain->capTypes.size();

    lua_rawgeti(L, LUA_REGISTRYINDEX, chain->midCallbackRef);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        // #13 — mid dead callback ref. The -2 sentinel was already filtered
        // above, so midCallbackRef >= 0 here: this is the genuine-lost-ref
        // case (closure GC'd / registry slot reused while the mid chain stayed
        // live). The mid goes inert — the captured instruction runs un-hooked
        // (g_midSkipOriginal stays clear). A degradation, not a crash-risk —
        // Warn, warn-once. Mid is one-per-VA in v1, so the dedup key is
        // targetVa (matching the off-thread mid keying above), not a per-entry
        // handleId. Hot-path-safe: the live path never reaches this block.
        if (DeadRefFirstObservation(chain->targetVa)) {
            log::WarnF(
                "hook_chain: mid '%s' (plugin '%s') at 0x%p — callback ref no "
                "longer a function (registry ref lost); mid inert — it will "
                "not fire (the captured instruction runs un-hooked). Warn-once "
                "per chain.",
                chain->midName.c_str(), chain->midPluginName.c_str(),
                (void*)target_func_ptr);
        }
        return 0;
    }
    const int top0 = lua_gettop(L);  // [..., fn]

    // Build the capture handle table.
    lua_createtable(L, (int)n, (int)n);
    const int tbl = lua_gettop(L);
    const bool nameKeyed = !chain->capNames.empty() &&
                           !chain->capNames[0].empty();
    for (size_t i = 0; i < n; ++i) {
        void* slot = payload + 16 * i;
        if (nameKeyed && i < chain->capNames.size() &&
            !chain->capNames[i].empty()) {
            PushCaptureHandle(L, slot, chain->capTypes[i].c_str());
            lua_setfield(L, tbl, chain->capNames[i].c_str());
        } else {
            PushCaptureHandle(L, slot, chain->capTypes[i].c_str());
            lua_rawseti(L, tbl, (int)i + 1);
        }
    }

    const int status = lua_pcall(L, 1, LUA_MULTRET, 0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("hook_chain: mid '%s' (plugin '%s') on 0x%p threw: %s",
                    chain->midName.c_str(), chain->midPluginName.c_str(),
                    (void*)target_func_ptr, msg ? msg : "<no message>");
        lua_settop(L, top0 - 1);
        return 0;
    }

    // Run-vs-skip from the return: "skip" (string) or true -> skip.
    const int retCount = lua_gettop(L) - (top0 - 1);
    if (retCount > 0) {
        if (lua_type(L, top0) == LUA_TSTRING) {
            const char* s = lua_tostring(L, top0);
            if (s && std::string(s) == "skip") g_midSkipOriginal = 1;
        } else if (lua_isboolean(L, top0) && lua_toboolean(L, top0)) {
            g_midSkipOriginal = 1;
        }
    }
    lua_settop(L, top0 - 1);  // balance

    LOG_DEBUG_KV("MID_HOOK", "hook_chain.mid_dispatch",
        log::KV("target",        (void*)target_func_ptr),
        log::KV("captures",      (int64_t)n),
        log::KV("skip_original", (int64_t)g_midSkipOriginal));
    return 0;
}

// ===========================================================================
// §7  Locator resolution + first-touch install + chain append
// ===========================================================================

// Resolve a HookPayload's function-entry locator to an absolute VA via
// the patch-engine locator pipeline (same path [[hook]]/kcdx.bytes use).
// Returns 0 + reason on failure.
uintptr_t ResolveLocator(const kcdx::hook_payload::HookPayload& p,
                         std::string& reason) {
    // Direct address — the VA is already in hand (pointer userdata or
    // integer the author got from kcdx.lua.cfunction_address /
    // scan_pattern / etc.). No resolution needed.
    if (p.address != 0) {
        return p.address;
    }
    // Address Library by human-readable NAME (address_id = "lua_pcall").
    // Resolves against the compiled-in library — the readable surface that
    // spares authors the opaque numeric id. Loud fail on miss (typo or
    // unknown name): a dead hook is worse UX than a clear error.
    if (!p.addressName.empty()) {
        // (owningAuthor, owningPlugin) drive the self > engine > other
        // precedence in ResolveByName: a bare
        // addressName resolves to this plugin's own target first, then the
        // engine seed, then any other plugin's target; an explicit form
        // resolves directly. Launch-time apply pass only — never a
        // hook-fire path.
        //
        // The binder now threads the real (author, plugin) pair through
        // HookPayload (step 4 of the 2-dot namespace refactor). The
        // empty-author transition has retired at THIS call site — what
        // remains transitional is plugin manifests whose [plugin].author
        // is still empty (step 6 populates them); when both are empty the
        // resolver walks the legacy 1-dot scope by (plugin, name) exactly
        // as before, so behavior is unchanged for the current corpus.
        uintptr_t va = kcdx::address_library::ResolveByName(
            p.addressName.c_str(), p.owningAuthor.c_str(),
            p.owningPlugin.c_str());
        if (va) {
            return va + (uintptr_t)(int64_t)p.offset;
        }
        // A 0 from ResolveByName has TWO meanings: (a) the name is genuinely
        // unresolvable (typo / wrong game version / unverified), or (b) the
        // name resolved by precedence to an author-declared target whose
        // locator is a Pattern or TargetSymbol — kinds address_library (a leaf
        // module) deliberately does NOT turn into a VA, because that would
        // make it depend on the patch engine / symbol table. Disambiguate by
        // asking for the resolved author-target descriptor (SAME self > engine
        // > other precedence + SAME collision-warn dedup as ResolveByName).
        // If it's a Pattern/TargetSymbol target, route its locatorStr through
        // the SAME patch::Resolve pipeline this function already owns for a
        // directly-set pattern / target_symbol — so an author names an AOB
        // site once and every plugin hooks it BY NAME end-to-end
        // (author-declared targets are shareable).
        // Same real (author, plugin) the ResolveByName call above
        // threads — the binder now carries both on HookPayload.
        const kcdx::address_library::AuthorTarget* at =
            kcdx::address_library::FindResolvedAuthorTarget(
                p.addressName.c_str(), p.owningAuthor.c_str(),
                p.owningPlugin.c_str());
        if (at &&
            (at->kind == kcdx::address_library::AuthorLocatorKind::Pattern ||
             at->kind == kcdx::address_library::AuthorLocatorKind::TargetSymbol)) {
            kcdx::patch::PatchEntry pe;
            pe.sourceFile = "<lua:kcdx.hook target=\"" + p.addressName + "\">";
            pe.name       = p.name;
            pe.module     = p.module;
            // Carry the consuming plugin so a routed target_symbol resolves by
            // the namespace model (self > other) in patch::Resolve.
            pe.pluginName = p.owningPlugin;
            // Feed the author target's locator into the field patch::Resolve
            // reads for that kind — the SAME path a directly-set locator uses.
            if (at->kind == kcdx::address_library::AuthorLocatorKind::Pattern) {
                // The pattern string is the raw author-supplied AOB (the
                // registry stores it un-parsed); parse it here. A malformed
                // pattern is an author error in the targets.toml row — turn the
                // ParsePattern throw into a clean Failed reason, never an
                // unwound apply pass.
                try {
                    pe.pattern = kcdx::patch::ParsePattern(at->locatorStr);
                } catch (const std::exception& ex) {
                    reason = "target '" + p.addressName +
                             "' (author-declared pattern) has a malformed AOB: " +
                             ex.what();
                    return 0;
                }
            } else {  // TargetSymbol
                pe.targetSymbol = at->locatorStr;
            }
            pe.offset = p.offset;  // the hook's own offset, applied after match
            pe.original.clear();
            pe.replacement.clear();
            kcdx::patch::ResolvedPatch r = kcdx::patch::Resolve(pe);
            if (!r.ok) {
                reason = "target '" + p.addressName +
                         "' is an author-declared target, but its locator did "
                         "not resolve: " + r.reason;
                return 0;
            }
            return r.patchAddr;
        }
        reason = "address_id name '" + p.addressName +
                 "' did not resolve in the Address Library (unknown "
                 "name, or its entry doesn't match this game version / "
                 "isn't verified). Check the name against kcdx.addr.*.";
        return 0;
    }
    // Build a PatchEntry carrying just the locator fields Resolve reads.
    kcdx::patch::PatchEntry pe;
    pe.sourceFile   = "<lua:kcdx.hook>";
    pe.name         = p.name;
    pe.module       = p.module;
    // Carry the consuming plugin so a directly-set target_symbol resolves by
    // the namespace model (self > other) in patch::Resolve.
    pe.pluginName   = p.owningPlugin;
    pe.pattern      = p.pattern;
    pe.context      = p.context;
    pe.anchor       = p.anchor;
    pe.maxAnchorDistance = p.maxAnchorDistance;
    pe.offset       = p.offset;
    pe.targetSymbol = p.targetSymbol;
    pe.addressId    = p.addressId;
    // Resolve checks original.size()==replacement.size(); give it equal
    // empties so the locator path runs.
    pe.original.clear();
    pe.replacement.clear();

    kcdx::patch::ResolvedPatch r = kcdx::patch::Resolve(pe);
    if (!r.ok) {
        reason = "locator did not resolve: " + r.reason;
        return 0;
    }
    return r.patchAddr;
}

// Build the JIT type-strings (return + params) from a parsed signature
// for make_jit_func / the call_original thunk. ABI-level: wstr/cstr are
// pointer-width (passed in a register as a pointer); the string<->Lua
// conversion is a marshaling concern handled in §4, not here.
void SignatureToAbiStrings(const kcdx::hook_signature::Signature& sig,
                           std::string&              returnTypeOut,
                           std::vector<std::string>& paramTypesOut) {
    returnTypeOut = SigTypeToJitString(sig.returnType);
    paramTypesOut.clear();
    for (const auto& a : sig.args) {
        // wstr/cstr -> "ptr" at the ABI (they're pointers in registers).
        kcdx::hook_signature::Type t = a.type;
        if (t == kcdx::hook_signature::Type::Wstr ||
            t == kcdx::hook_signature::Type::Cstr) {
            paramTypesOut.emplace_back("ptr");
        } else {
            paramTypesOut.emplace_back(SigTypeToJitString(t));
        }
    }
}

// Same, but for the return type's ABI string (wstr/cstr -> ptr).
std::string ReturnAbiString(const kcdx::hook_signature::Signature& sig) {
    if (sig.returnType == kcdx::hook_signature::Type::Wstr ||
        sig.returnType == kcdx::hook_signature::Type::Cstr) {
        return "ptr";
    }
    return SigTypeToJitString(sig.returnType);
}

// ---------------------------------------------------------------------------
// Callsite locator resolution + E8-displacement rewrite (mode="callsite")
// ---------------------------------------------------------------------------

// Resolve a CallsiteLocator to the absolute VA of the CALL instruction
// whose rel32 displacement we will rewrite. Exactly one of pattern /
// addressId / rva is set (the binder's ValidateLocator guarantees this).
//   pattern    : run it through the patch-engine locator pipeline (same
//                path function-entry hooks use), then apply the locator's
//                own `offset` (the offset to the call opcode in the match).
//   addressId  : Address Library numeric id -> VA, + offset.
//   rva        : "Module.dll @ rva 0xNNNN" -> module_base + rva (the
//                escape-hatch form; no library entry needed).
// Returns 0 + reason on failure.
uintptr_t ResolveCallsite(const kcdx::hook_payload::HookPayload& p,
                          std::string& reason) {
    const kcdx::hook_payload::CallsiteLocator& cs = *p.callsite;

    // rva form: "Module.dll @ rva 0x12345a"  (case-insensitive "rva").
    if (!cs.rva.empty()) {
        // Parse "<module> @ rva <hex>". Be forgiving about whitespace.
        std::string s = cs.rva;
        const std::string::size_type at = s.find('@');
        if (at == std::string::npos) {
            reason = "target_callsite.rva must be of the form "
                     "\"WHGame.dll @ rva 0x12345a\" (module, '@', then "
                     "'rva <hex offset>'); got: " + cs.rva;
            return 0;
        }
        std::string moduleName = s.substr(0, at);
        // trim trailing spaces from module name
        while (!moduleName.empty() &&
               (moduleName.back() == ' ' || moduleName.back() == '\t'))
            moduleName.pop_back();
        std::string rest = s.substr(at + 1);  // " rva 0x12345a"
        // find the hex token after "rva"
        const std::string::size_type rvaKw = rest.find("rva");
        if (rvaKw == std::string::npos) {
            reason = "target_callsite.rva is missing the 'rva' keyword "
                     "(expected \"<module> @ rva 0x...\"); got: " + cs.rva;
            return 0;
        }
        std::string hexPart = rest.substr(rvaKw + 3);  // after "rva"
        // strtoull handles optional leading 0x and skips leading spaces.
        char* end = nullptr;
        unsigned long long rvaVal = std::strtoull(hexPart.c_str(), &end, 0);
        if (end == hexPart.c_str() || rvaVal == 0ull) {
            reason = "target_callsite.rva offset did not parse as a "
                     "non-zero number (expected a hex RVA like 0x12345a); "
                     "got: " + cs.rva;
            return 0;
        }
        if (moduleName.empty()) moduleName = p.module;  // default WHGame.dll
        std::wstring wmod(moduleName.begin(), moduleName.end());
        kcdx::pe::ModuleView mod;
        if (!kcdx::pe::OpenModule(wmod.c_str(), mod)) {
            reason = "target_callsite.rva module '" + moduleName +
                     "' is not loaded";
            return 0;
        }
        return reinterpret_cast<uintptr_t>(mod.baseBytes) +
               static_cast<uintptr_t>(rvaVal) +
               static_cast<uintptr_t>(static_cast<int64_t>(cs.offset));
    }

    // address_id form: numeric kcdx_id in the refdb cache.
    if (cs.addressId != 0) {
        uintptr_t va = kcdx::refdb::ResolveAddrById(cs.addressId);
        if (!va) {
            reason = "target_callsite.address_id " +
                     std::to_string((unsigned long long)cs.addressId) +
                     " did not resolve in the refdb cache (unknown kcdx_id, "
                     "row carries no rva, or WHGame.dll not mapped)";
            return 0;
        }
        return va + static_cast<uintptr_t>(static_cast<int64_t>(cs.offset));
    }

    // pattern form: run through the patch-engine locator pipeline.
    kcdx::patch::PatchEntry pe;
    pe.sourceFile = "<lua:kcdx.hook callsite>";
    pe.name       = p.name;
    pe.module     = p.module;
    pe.pattern    = cs.pattern;
    pe.context    = p.context;       // optional disambiguation (shared field)
    pe.anchor     = p.anchor;
    pe.maxAnchorDistance = p.maxAnchorDistance;
    pe.offset     = cs.offset;       // offset to the call opcode in the match
    pe.original.clear();
    pe.replacement.clear();
    kcdx::patch::ResolvedPatch r = kcdx::patch::Resolve(pe);
    if (!r.ok) {
        reason = "target_callsite.pattern did not resolve: " + r.reason;
        return 0;
    }
    return r.patchAddr;
}

// Rewrite the 4 rel32 displacement bytes of the E8 call at
// callsiteVa+1..+4 so the call now targets `newTarget`. The opcode byte
// at callsiteVa is assumed already verified == 0xE8 by the caller.
// VirtualProtect dance + FlushInstructionCache (same shape as
// patch_engine::WriteBytesAtAddr, which is TU-local there). Returns false
// + reason on failure.
bool RewriteCallDisplacement(uintptr_t callsiteVa, uintptr_t newTarget,
                             std::string& reason) {
    // rel32 = newTarget - (callsiteVa + 5). Must fit in a signed 32-bit.
    const int64_t rel =
        static_cast<int64_t>(newTarget) -
        static_cast<int64_t>(callsiteVa + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "trampoline 0x%p is not rel32-reachable from the call site "
            "0x%p (distance %lld bytes > 2GB)",
            (void*)newTarget, (void*)callsiteVa, (long long)rel);
        reason = buf;
        return false;
    }
    const int32_t disp32 = static_cast<int32_t>(rel);
    uintptr_t writeAt = callsiteVa + 1;
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(writeAt), 4,
                        PAGE_EXECUTE_READWRITE, &oldProt)) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "VirtualProtect failed at 0x%p (err=%lu)",
            (void*)writeAt, (unsigned long)GetLastError());
        reason = buf;
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(writeAt), &disp32, 4);
    DWORD restoreOld = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(writeAt), 4, oldProt, &restoreOld);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(writeAt), 4);
    return true;
}

// Insert an entry into the chain in load order.
//
// Engine entries (Kind::Engine — every kcdx engine-internal AddC install
// via AddCEngine: lua_pcall, frealloc canary, ModManager_ctor, BugSplat
// ctor, SaveGame, LoadGame) always sort to the FRONT of the chain, ahead
// of every plugin entry, regardless of either side's declared priority.
// The engine-always-first invariant is type-enforced via the isEngine
// flag — NOT via a priority sentinel that any plugin could spoof. Within
// the engine block and within the plugin block the existing
// (priority asc, name asc) tiebreak applies, so two engine entries (or
// two plugin entries) sort exactly as before.
//
// Engine-vs-plugin: engine always wins. Engine-vs-engine: priority + name.
// Plugin-vs-plugin: priority + name (unchanged from the v0.1 comparator).
void InsertOrdered(Chain& chain, ChainEntry&& e) {
    auto pos = chain.entries.begin();
    for (; pos != chain.entries.end(); ++pos) {
        const bool eIsEngine = e.isEngine;
        const bool pIsEngine = pos->isEngine;
        if (eIsEngine && !pIsEngine) break;     // engine entry goes before plugin
        if (!eIsEngine && pIsEngine) continue;  // plugin entry sorts after engine
        // Same kind: existing (priority asc, name asc) tiebreak.
        if (e.priority < pos->priority) break;
        if (e.priority == pos->priority && e.name < pos->name) break;
    }
    chain.entries.insert(pos, std::move(e));
}

}  // namespace

// Byte-compatibility of two signatures for sharing ONE marshaling thunk
// (§1.1 of the spec). v1 rule: same arg count and each slot maps to the
// same JIT type-string + same return type-string. This is conservative
// (e.g. i32 vs i64 both -> "i64" so they're treated compatible, which is
// fine — the register move is identical) and never produces a wrong
// marshal. Lives here (not the anon namespace) so the public
// kcdx::hook_signature::SignaturesCompatible wrapper below can forward to
// it; defined in kcdx::hook_chain so the anon-namespace SigTypeToJitString
// table resolves by unqualified lookup (minimal-blast — no header churn
// for that file-local type→string map). The in-TU chain-share check
// (Add/AddC §3) calls SignaturesCompatibleImpl directly.
bool SignaturesCompatibleImpl(const kcdx::hook_signature::Signature& a,
                              const kcdx::hook_signature::Signature& b) {
    if (a.args.size() != b.args.size()) return false;
    if (SigTypeToJitString(a.returnType) !=
        std::string(SigTypeToJitString(b.returnType))) return false;
    for (size_t i = 0; i < a.args.size(); ++i) {
        if (std::string(SigTypeToJitString(a.args[i].type)) !=
            SigTypeToJitString(b.args[i].type)) return false;
    }
    return true;
}

void SetLuaState(lua_State* L) {
    g_L = L;
    if (L) {
        // Capture the game main thread id ONCE on the first non-null
        // L invocation. This callsite executes inside hooks.cpp's
        // first-update-tick handler (HookedUpdate, line ~316-321), so
        // by construction the calling thread IS the game's main /
        // per-frame update / Lua-callback thread.
        //
        // The static-bool sentinel is defensive — SetGameMainThread
        // itself is idempotent (every call lands the same tid because
        // the first-update-tick hook always fires on the same
        // thread), but avoiding the re-write keeps the intent
        // explicit and makes a future "fires from a different thread
        // on re-entry" regression visibly skip rather than silently
        // overwrite. See log.h SetGameMainThread doc-comment.
        static bool s_gameMainThreadCaptured = false;
        if (!s_gameMainThreadCaptured) {
            log::SetGameMainThread();
            s_gameMainThreadCaptured = true;
        }
        // Create the mid-capture-handle metatable once on the live
        // state, so MidDispatch can hand the callback handles with
        // :get()/:set(). Raw Lua C API only (no kcdx-side static-const
        // sentinel on the shared lua_State).
        EnsureCaptureHandleMetatable(L);
    }
}

// Install a mode=mid hook: a mid-function detour at the captured-
// instruction VA (payload's locator already resolved to it, offset
// included), built with make_jit_midfunc + MidDispatch. v1 keeps one mid
// hook per VA — the JIT bakes one capture layout, so a second mid hook on
// the same VA loses by load order (it cannot share the layout). This is
// the safe-but-blunt v1, consistent with the around/replace exclusivity;
// footprint-based mid coexistence is the same future work as the
// signature path (smart-replace-conflict-detection.md). The runtime_func_t
// holds the detour for the session.
AddResult AddMid(const kcdx::hook_payload::HookPayload& payload,
                 int callbackRef, const std::string& pluginName,
                 int priority, const std::string& name,
                 uint64_t handleId) {
    AddResult res;
    (void)priority;  // v1: one mid hook per VA, so ordering is moot

    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    if (FindChain(targetVa)) {
        // A hook (mid or signature) already owns this VA. v1 mid can't
        // share — load-order-loses: the later one fails loud.
        res.reason =
            "target already has a hook; a 'mid' hook needs sole ownership "
            "of its capture site in v1 (the JIT bakes one capture layout). "
            "The earlier hook wins by load order. (Footprint-based mid "
            "coexistence is future work.)";
        return res;
    }

    // Auto-decode the resume offset (how many bytes to skip past so the
    // resume lands on an instruction boundary beyond MinHook's patched
    // region). hde64-disassemble forward from the capture site until the
    // accumulated length covers MinHook's minimum 5-byte rel32 jmp. Same
    // algorithm as hook_engine::ApplyOneMidHook — better UX than making
    // the author count instruction bytes.
    constexpr int kMinHookPatchBytes = 5;
    int stackRestoreOffset = 0;
    {
        uintptr_t scan = targetVa;
        int accumulated = 0;
        while (accumulated < kMinHookPatchBytes) {
            hde64s hs{};
            unsigned int len =
                hde64_disasm(reinterpret_cast<const void*>(scan), &hs);
            if (len == 0 || (hs.flags & F_ERROR) != 0) {
                res.reason =
                    "could not disassemble the capture site to compute the "
                    "resume point (hde64 failed at the mid offset); the "
                    "`offset` may not land on an instruction boundary";
                return res;
            }
            scan += len;
            accumulated += static_cast<int>(len);
        }
        stackRestoreOffset = accumulated;
    }
    const uintptr_t resumeAddr = targetVa + (uintptr_t)stackRestoreOffset;

    auto newChain = std::make_unique<Chain>();
    newChain->targetVa       = targetVa;
    newChain->isMid          = true;
    newChain->midCallbackRef = callbackRef;
    newChain->midHandleId    = handleId;
    newChain->midPluginName  = pluginName;
    newChain->midName        = name;
    newChain->capExprs       = payload.captureExprs;
    newChain->capTypes       = payload.captureTypes;
    newChain->capNames       = payload.captureNames;
    newChain->midOffThread   = payload.offThread;
    newChain->rf             = std::make_unique<kcdx::rom::runtime_func_t>();

    // call_original_mode = 2 (Auto): the JIT pushes MinHook's trampoline
    // by default and, after MidDispatch returns, reads our skip-flag byte;
    // if set, it resumes past the captured instruction instead. This is
    // the return-value model (the callback returns "skip"/true to skip).
    uintptr_t jit = newChain->rf->make_jit_midfunc(
        newChain->capTypes,
        newChain->capExprs,
        stackRestoreOffset,
        /*call_original_mode=*/2,
        /*skip_flag_addr=*/reinterpret_cast<uintptr_t>(&g_midSkipOriginal),
        resumeAddr,
        asmjit::Arch::kX64,
        &MidDispatch,
        targetVa);
    if (!jit) {
        res.reason = "make_jit_midfunc failed (capture/codegen — check the "
                     "capture exprs + types; see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    // Wire MinHook's pOriginal into the JIT trampoline's call-original
    // slot — Auto mode's default path rets into it (runs the captured
    // instruction). Without this the trampoline reads null and rets to 0.
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    } else {
        // #14 — null call-original slot. The runtime_func_t ctor
        // default-constructs the detour non-null, so this is normally
        // unreachable; but a null slot means the JIT trampoline's
        // call-original path reads null and any around / auto-mid that rets
        // into it jumps to 0 → crash. Pre-Batch-E this silently proceeded and
        // reported install SUCCESS (the two AddCallsite variants already failed
        // the install here; the others did not). Make them CONSISTENT: fail the
        // install with a reason, exactly as AddCallsite does — Error-class
        // (a later around/auto-mid on this target would deref null and crash).
        res.reason = "internal: runtime_func_t has no call-original slot "
                     "(detour_hook missing) — a later around/auto-mid on this "
                     "target would deref null and crash; install aborted";
        return res;
    }

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed mid '%s' (plugin '%s') at 0x%p "
               "(%zu captures, resume +%d, JIT detour 0x%p)",
               name.c_str(), pluginName.c_str(), (void*)targetVa,
               payload.captureExprs.size(), stackRestoreOffset, (void*)jit);
    return res;
}

// Install a mode="callsite" hook: redirect ONE E8 near-call so only that
// caller reaches the chain trampoline; every other caller of the same
// callee is untouched. Reuses the function-entry dispatch spine
// (DispatchPre/DispatchPost + the entries chain + the call_original
// thunk over the ORIGINAL CALLEE) — the only difference from Add()'s
// first-touch path is the install: instead of a MinHook detour on the
// callee, we point the call site's E8 rel32 at the chain trampoline and
// wire the trampoline's call-original slot to the original callee VA.
//
// The Chain is keyed in g_chains by the CALL-SITE VA (not the callee), so
// a second callsite hook on the SAME site chains/mediates, while two
// callsite hooks on DIFFERENT sites that call the same callee never
// collide — the defining property of callsite vs function-entry.
AddResult AddCallsite(const kcdx::hook_payload::HookPayload& payload,
                      int callbackRef, const std::string& pluginName,
                      int priority, const std::string& name,
                      uint64_t handleId) {
    using Mode = kcdx::hook_payload::Mode;
    AddResult res;

    if (!payload.hasSignature) {
        // Should be unreachable — the binder requires a signature for the
        // before/after/around/replace behaviors callsite uses — but guard.
        res.reason = "internal: callsite hook has no parsed signature";
        return res;
    }

    // 1. Resolve the call-site VA (the E8 instruction).
    std::string reason;
    uintptr_t callsiteVa = ResolveCallsite(payload, reason);
    if (!callsiteVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    // 2. Existing callsite chain on this exact site? Chain onto it (same
    //    coexistence rules as a function-entry chain — the E8 already
    //    points at that chain's trampoline; we only append a behavior).
    if (Chain* chain = FindChain(callsiteVa)) {
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, payload.signature,
                        /*incomingIsCallsite=*/true, whyNot)) {
            // Record the loser on the winning chain (same name/priority
            // this entry would have had + the same reason res surfaces),
            // so the conflict report lists it with applied=false. Record
            // first, then move whyNot into res.reason (single source).
            chain->rejected.push_back({name, priority, whyNot});
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.mode = payload.mode; e.callbackRef = callbackRef;
        e.pluginName = pluginName; e.priority = priority; e.name = name;
        e.handleId = handleId;
        e.offThread = payload.offThread;
        const bool needsCallOriginal = (payload.mode == Mode::Around);
        InsertOrdered(*chain, std::move(e));
        if (needsCallOriginal && !chain->callOriginalThunk &&
            chain->calleeVa) {
            std::string rt; std::vector<std::string> pts;
            SignatureToAbiStrings(chain->sig, rt, pts);
            chain->callOriginalThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    chain->calleeVa, rt, pts);
        }
        res.ok = true;
        log::InfoF("hook_chain: appended %s '%s' (plugin '%s') to CALLSITE "
                   "0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)callsiteVa,
                   chain->entries.size());
        return res;
    }

    // 3. First touch on this call site. Read the opcode — v1 handles ONLY
    //    the E8 near-call rel32 form. FF /2 (call r/m), FF 15 (call
    //    [rip+disp]) and other indirect calls are out of scope: their
    //    displacement is not a rel32-to-callee we can recompute, so reject
    //    LOUDLY naming the actual opcode (this is an ABI fact verified at
    //    install, never assumed — probe the binary, don't theorize).
    const uint8_t opcode = *reinterpret_cast<const uint8_t*>(callsiteVa);
    if (opcode != 0xE8) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "callsite at 0x%p is not an E8 near-call rel32 (opcode byte is "
            "0x%02X). mode='callsite' v1 only redirects direct E8 calls; "
            "indirect calls (FF /2 register/memory, FF 15 [rip+disp]) are "
            "out of scope. Check the target_callsite locator points at the "
            "CALL instruction (offset to the E8 byte).",
            (void*)callsiteVa, (unsigned)opcode);
        res.reason = buf;
        return res;
    }

    // 4. Original callee VA = (callsiteVa + 5) + signed disp32 at +1.
    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(callsiteVa + 1), 4);
    const uintptr_t calleeVa =
        callsiteVa + 5 + static_cast<uintptr_t>(static_cast<int64_t>(disp));

    // 5. Build the chain trampoline over the ORIGINAL CALLEE, reusing the
    //    function-entry spine. make_jit_func bakes the address of the
    //    runtime_func_t's own detour `original_` slot into the emitted
    //    call-original path; we then write the callee VA into that slot
    //    (no MinHook detour is installed — this trampoline is a standalone
    //    function the E8 will point at). The slot is valid even without
    //    InstallRuntime (the detour_hook is default-constructed in the
    //    runtime_func_t ctor; get_jit_original_slot() == &original_).
    auto newChain = std::make_unique<Chain>();
    newChain->targetVa   = callsiteVa;
    newChain->isCallsite = true;
    newChain->calleeVa   = calleeVa;
    newChain->sig        = payload.signature;
    newChain->rf         = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(payload.signature, rt, pts);

    // The last make_jit_func arg is the value baked into the trampoline
    // and passed to DispatchPre/DispatchPost as `target_func_ptr` — it is
    // the CHAIN-LOOKUP KEY (ResolveChainForDispatch keys g_chains by it),
    // NOT the call target. For a callsite chain that key is the call-site
    // VA (how this chain is stored in g_chains), so the dispatchers find
    // THIS chain. The original callee VA is supplied separately via the
    // call-original slot (below) + the around thunk.
    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, /*target_func_ptr=*/callsiteVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }
    // Wire the original callee VA into the trampoline's call-original slot
    // so the spine's auto-run-original path (before/after) and the around
    // call_original thunk reach the real callee.
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = reinterpret_cast<void*>(calleeVa);
    } else {
        res.reason = "callsite: runtime_func_t has no call-original slot "
                     "(internal — detour_hook missing)";
        return res;
    }

    // 6. around's call_original thunk runs over the ORIGINAL CALLEE VA
    //    directly (we have it in hand; no MinHook trampoline involved).
    if (payload.mode == Mode::Around) {
        newChain->callOriginalThunk = (uintptr_t)
            kcdx::dynamic_call_jit::BuildLuaCallThunk(calleeVa, rt, pts);
    }

    // 7. Verify rel32 reachability + rewrite the E8 displacement to the
    //    trampoline. RewriteCallDisplacement computes the distance and
    //    fails loud if the trampoline is out of rel32 range (the branch
    //    pool guarantees proximity to WHGame.dll, but the call site may be
    //    in another module — verified here, not assumed). On success the
    //    call site now reaches the chain trampoline; the conflict-engine
    //    footprint for the 4 rewritten bytes is the g_chains entry keyed
    //    by callsiteVa (CanCoexist mediates a second redirect of the same
    //    site — load-order-loses).
    if (!RewriteCallDisplacement(callsiteVa, jit, reason)) {
        res.reason = "callsite redirect failed: " + reason;
        return res;
    }

    ChainEntry e;
    e.mode = payload.mode; e.callbackRef = callbackRef;
    e.pluginName = pluginName; e.priority = priority; e.name = name;
    e.handleId = handleId;
    e.offThread = payload.offThread;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(callsiteVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed CALLSITE %s '%s' (plugin '%s') at E8 "
               "site 0x%p -> callee 0x%p (trampoline 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)callsiteVa, (void*)calleeVa,
               (void*)jit);
    return res;
}

AddResult Add(lua_State*                             L,
              const kcdx::hook_payload::HookPayload& payload,
              int                                    callbackRef,
              const std::string&                     pluginName,
              int                                    priority,
              const std::string&                     name,
              uint64_t                               handleId) {
    AddResult res;
    if (L) g_L = L;  // capture the dispatch state on first use

    // mode=mid is a different install (mid-function detour + captures, no
    // function signature) — branch before the signature gate.
    if (payload.mode == kcdx::hook_payload::Mode::Mid) {
        return AddMid(payload, callbackRef, pluginName, priority, name,
                      handleId);
    }

    // mode="callsite" is a different install (rewrite ONE E8 call's rel32
    // displacement to a chain trampoline; the callee is untouched, so only
    // THIS caller is affected). The behavior (before/after/around/replace)
    // in payload.mode drives the dispatch semantics exactly as for a
    // function-entry hook — AddCallsite reuses the same DispatchPre/Post +
    // call_original spine. Branch after the Mid check (callsite uses a
    // signature, so the signature gate below would also pass; routing on
    // the scope keeps the install path explicit).
    if (payload.callsiteScope) {
        return AddCallsite(payload, callbackRef, pluginName, priority, name,
                           handleId);
    }

    if (!payload.hasSignature) {
        res.reason = "internal: hook has no parsed signature";
        return res;
    }

    // Resolve the target VA.
    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    Chain* chain = FindChain(targetVa);

    if (chain) {
        // Existing target — check coexistence, then append.
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, payload.signature,
                        /*incomingIsCallsite=*/false, whyNot)) {
            // Record the loser on the winning chain (same name/priority
            // this entry would have had + the same reason res surfaces),
            // so the conflict report lists it with applied=false. Record
            // first, then move whyNot into res.reason (single source).
            chain->rejected.push_back({name, priority, whyNot});
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.mode = payload.mode; e.callbackRef = callbackRef;
        e.pluginName = pluginName; e.priority = priority; e.name = name;
        e.handleId = handleId;
        e.offThread = payload.offThread;
        const bool needsCallOriginal =
            (payload.mode == kcdx::hook_payload::Mode::Around);
        InsertOrdered(*chain, std::move(e));
        // Build the call_original thunk if this is the first around and
        // we haven't built one yet.
        if (needsCallOriginal && !chain->callOriginalThunk) {
            void** origSlot = chain->rf->get_jit_original_slot();
            if (origSlot && *origSlot) {
                std::string rt; std::vector<std::string> pts;
                SignatureToAbiStrings(chain->sig, rt, pts);
                lua_CFunction th = kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    (uintptr_t)*origSlot, rt, pts);
                chain->callOriginalThunk = (uintptr_t)th;
            }
        }
        res.ok = true;
        log::InfoF("hook_chain: appended %s '%s' (plugin '%s') to target "
                   "0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)targetVa,
                   chain->entries.size());
        return res;
    }

    // First touch — build the detour for this target.
    auto newChain = std::make_unique<Chain>();
    newChain->targetVa = targetVa;
    newChain->sig      = payload.signature;
    newChain->rf       = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(payload.signature, rt, pts);

    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, targetVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    // Wire MinHook's pOriginal into the JIT trampoline's call-original
    // slot (same as the dynamic_hook path; without this the thunk's
    // call-through reads null).
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    } else {
        // #14 — null call-original slot. The runtime_func_t ctor
        // default-constructs the detour non-null, so this is normally
        // unreachable; but a null slot means the JIT trampoline's
        // call-original path reads null and any around / auto-mid that rets
        // into it jumps to 0 → crash. Pre-Batch-E this silently proceeded and
        // reported install SUCCESS (the two AddCallsite variants already failed
        // the install here; the others did not). Make them CONSISTENT: fail the
        // install with a reason, exactly as AddCallsite does — Error-class
        // (a later around/auto-mid on this target would deref null and crash).
        res.reason = "internal: runtime_func_t has no call-original slot "
                     "(detour_hook missing) — a later around/auto-mid on this "
                     "target would deref null and crash; install aborted";
        return res;
    }

    // Build the call_original thunk now if this first hook is an around.
    if (payload.mode == kcdx::hook_payload::Mode::Around) {
        if (install.pOriginal) {
            newChain->callOriginalThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    (uintptr_t)install.pOriginal, rt, pts);
        }
    }

    ChainEntry e;
    e.mode = payload.mode; e.callbackRef = callbackRef;
    e.pluginName = pluginName; e.priority = priority; e.name = name;
    e.handleId = handleId;
    e.offThread = payload.offThread;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed %s '%s' (plugin '%s') at target 0x%p "
               "(JIT detour 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)targetVa, (void*)jit);
    return res;
}

// ===========================================================================
// AddC / AddCMid / AddCCallsite — C-side parallels of Add/AddMid/AddCallsite.
// ===========================================================================
//
// Same chain model, same coexistence rules, same load-order ordering —
// the only difference is the per-entry construction: C entries carry
// (cFn, cSig, cDispatchThunk) instead of (callbackRef); C mids carry
// (midCFn, midCSig, midCDispatchThunk); C arounds + first-touch C
// callsites build callOriginalCThunk via BuildNativeCallThunk instead
// of the Lua-shaped BuildLuaCallThunk.
//
// Off-thread routing rides on payload.offThread → entry.offThread (or
// chain.midOffThread). Per the locked decisions, the C path uses the
// SAME warn-once-skip
// degradation the Lua path uses; no separate v1 path.

AddResult AddCMid(const kcdx::hook_payload::HookPayload& payload,
                  void*                                  cFn,
                  const kcdx::hook_signature::Signature& cSig,
                  const std::string& pluginName,
                  int                priority,
                  const std::string& name,
                  uint64_t           handleId) {
    AddResult res;
    (void)priority;  // v1: one mid hook per VA, ordering is moot

    // Consume any engine-stamp flag set on this thread by AddCEngine.
    // Mid hooks live on Chain itself (not in entries) — the stamp lands
    // on chain->isMidEngine (set below at the chain-construction site).
    // No engine mid sites exist today (the engine-direct migration covers
    // function-entry only: lua_pcall / frealloc / ModManager_ctor /
    // BugSplat ctor / Save / Load), but the stamp routing is in place so
    // a future engine mid site stamps the engine identity correctly +
    // benefits from the dead-classifier carve-out at MidDispatch.
    const bool midStamp = TakeEngineStamp();

    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);
    if (FindChain(targetVa)) {
        res.reason =
            "target already has a hook; a 'mid' hook needs sole ownership "
            "of its capture site in v1 (the JIT bakes one capture layout). "
            "The earlier hook wins by load order. (Footprint-based mid "
            "coexistence is future work.)";
        return res;
    }

    constexpr int kMinHookPatchBytes = 5;
    int stackRestoreOffset = 0;
    {
        uintptr_t scan = targetVa;
        int accumulated = 0;
        while (accumulated < kMinHookPatchBytes) {
            hde64s hs{};
            unsigned int len =
                hde64_disasm(reinterpret_cast<const void*>(scan), &hs);
            if (len == 0 || (hs.flags & F_ERROR) != 0) {
                res.reason =
                    "could not disassemble the capture site to compute the "
                    "resume point (hde64 failed at the mid offset); the "
                    "`offset` may not land on an instruction boundary";
                return res;
            }
            scan += len;
            accumulated += static_cast<int>(len);
        }
        stackRestoreOffset = accumulated;
    }
    const uintptr_t resumeAddr = targetVa + (uintptr_t)stackRestoreOffset;

    auto newChain = std::make_unique<Chain>();
    newChain->targetVa          = targetVa;
    newChain->isMid             = true;
    newChain->midKind           = ChainEntry::Kind::C;
    newChain->midCFn            = cFn;
    newChain->midCSig           = cSig;
    newChain->midHandleId       = handleId;
    newChain->midPluginName     = pluginName;
    newChain->midName           = name;
    newChain->capExprs          = payload.captureExprs;
    newChain->capTypes          = payload.captureTypes;
    newChain->capNames          = payload.captureNames;
    newChain->midOffThread      = payload.offThread;
    newChain->isMidEngine       = midStamp;
    newChain->rf                = std::make_unique<kcdx::rom::runtime_func_t>();
    newChain->midCDispatchThunk =
        kcdx::dynamic_call_jit::BuildCDispatchThunk(
            cFn, cSig, kcdx::hook_payload::Mode::Mid);
    if (!newChain->midCDispatchThunk) {
        res.reason = "BuildCDispatchThunk(Mid) failed (see kcdx.log)";
        return res;
    }

    uintptr_t jit = newChain->rf->make_jit_midfunc(
        newChain->capTypes,
        newChain->capExprs,
        stackRestoreOffset,
        /*call_original_mode=*/2,
        /*skip_flag_addr=*/reinterpret_cast<uintptr_t>(&g_midSkipOriginal),
        resumeAddr,
        asmjit::Arch::kX64,
        &MidDispatch,
        targetVa);
    if (!jit) {
        res.reason = "make_jit_midfunc failed (capture/codegen — check the "
                     "capture exprs + types; see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    } else {
        // #14 — null call-original slot. The runtime_func_t ctor
        // default-constructs the detour non-null, so this is normally
        // unreachable; but a null slot means the JIT trampoline's
        // call-original path reads null and any around / auto-mid that rets
        // into it jumps to 0 → crash. Pre-Batch-E this silently proceeded and
        // reported install SUCCESS (the two AddCallsite variants already failed
        // the install here; the others did not). Make them CONSISTENT: fail the
        // install with a reason, exactly as AddCallsite does — Error-class
        // (a later around/auto-mid on this target would deref null and crash).
        res.reason = "internal: runtime_func_t has no call-original slot "
                     "(detour_hook missing) — a later around/auto-mid on this "
                     "target would deref null and crash; install aborted";
        return res;
    }

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed C mid '%s' (plugin '%s') at 0x%p "
               "(%zu captures, resume +%d, JIT detour 0x%p)",
               name.c_str(), pluginName.c_str(), (void*)targetVa,
               payload.captureExprs.size(), stackRestoreOffset, (void*)jit);
    return res;
}

AddResult AddCCallsite(const kcdx::hook_payload::HookPayload& payload,
                       void*                                  cFn,
                       const kcdx::hook_signature::Signature& cSig,
                       const std::string& pluginName,
                       int                priority,
                       const std::string& name,
                       uint64_t           handleId) {
    using Mode = kcdx::hook_payload::Mode;
    AddResult res;

    if (!payload.hasSignature) {
        res.reason = "internal: C callsite hook has no parsed signature";
        return res;
    }

    // Consume the engine stamp; same one-shot contract as AddC's read.
    // (Engine callsite migrations are not in scope today, but the path
    // is symmetric for the future — a missing read would silently drop
    // the engine identity on an engine-callsite install.)
    const bool stamp = TakeEngineStamp();

    std::string reason;
    uintptr_t callsiteVa = ResolveCallsite(payload, reason);
    if (!callsiteVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    // Existing callsite chain on this exact site? Chain onto it.
    if (Chain* chain = FindChain(callsiteVa)) {
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, cSig,
                        /*incomingIsCallsite=*/true, whyNot)) {
            // Record the loser on the winning chain (same name/priority
            // this entry would have had + the same reason res surfaces),
            // so the conflict report lists it with applied=false. Record
            // first, then move whyNot into res.reason (single source).
            chain->rejected.push_back({name, priority, whyNot});
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.kind           = ChainEntry::Kind::C;
        e.mode           = payload.mode;
        e.cFn            = cFn;
        e.cSig           = cSig;
        e.cDispatchThunk = kcdx::dynamic_call_jit::BuildCDispatchThunk(
            cFn, cSig, payload.mode);
        if (!e.cDispatchThunk) {
            res.reason = "BuildCDispatchThunk failed (see kcdx.log)";
            return res;
        }
        e.pluginName = pluginName;
        e.priority   = priority;
        e.name       = name;
        e.handleId   = handleId;
        e.offThread  = payload.offThread;
        e.isEngine   = stamp;
        const bool needsCallOriginal = (payload.mode == Mode::Around);
        InsertOrdered(*chain, std::move(e));
        if (needsCallOriginal && !chain->callOriginalCThunk &&
            chain->calleeVa) {
            std::string rt; std::vector<std::string> pts;
            SignatureToAbiStrings(chain->sig, rt, pts);
            chain->callOriginalCThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildNativeCallThunk(
                    chain->calleeVa, rt, pts);
        }
        res.ok = true;
        log::InfoF("hook_chain: appended C %s '%s' (plugin '%s') to "
                   "CALLSITE 0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)callsiteVa,
                   chain->entries.size());
        return res;
    }

    // First touch on this call site — verify it's an E8 near-call.
    const uint8_t opcode = *reinterpret_cast<const uint8_t*>(callsiteVa);
    if (opcode != 0xE8) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "callsite at 0x%p is not an E8 near-call rel32 (opcode byte is "
            "0x%02X). C Callsite v1 only redirects direct E8 calls; "
            "indirect calls (FF /2 register/memory, FF 15 [rip+disp]) are "
            "out of scope.",
            (void*)callsiteVa, (unsigned)opcode);
        res.reason = buf;
        return res;
    }

    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(callsiteVa + 1), 4);
    const uintptr_t calleeVa =
        callsiteVa + 5 + static_cast<uintptr_t>(static_cast<int64_t>(disp));

    auto newChain = std::make_unique<Chain>();
    newChain->targetVa   = callsiteVa;
    newChain->isCallsite = true;
    newChain->calleeVa   = calleeVa;
    newChain->sig        = cSig;
    newChain->rf         = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(cSig, rt, pts);

    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, /*target_func_ptr=*/callsiteVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = reinterpret_cast<void*>(calleeVa);
    } else {
        res.reason = "callsite: runtime_func_t has no call-original slot "
                     "(internal — detour_hook missing)";
        return res;
    }

    if (payload.mode == Mode::Around) {
        newChain->callOriginalCThunk = (uintptr_t)
            kcdx::dynamic_call_jit::BuildNativeCallThunk(calleeVa, rt, pts);
    }

    if (!RewriteCallDisplacement(callsiteVa, jit, reason)) {
        res.reason = "callsite redirect failed: " + reason;
        return res;
    }

    ChainEntry e;
    e.kind           = ChainEntry::Kind::C;
    e.mode           = payload.mode;
    e.cFn            = cFn;
    e.cSig           = cSig;
    e.cDispatchThunk = kcdx::dynamic_call_jit::BuildCDispatchThunk(
        cFn, cSig, payload.mode);
    if (!e.cDispatchThunk) {
        res.reason = "BuildCDispatchThunk failed (see kcdx.log)";
        return res;
    }
    e.pluginName = pluginName;
    e.priority   = priority;
    e.name       = name;
    e.handleId   = handleId;
    e.offThread  = payload.offThread;
    e.isEngine   = stamp;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(callsiteVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed C CALLSITE %s '%s' (plugin '%s') at "
               "E8 site 0x%p -> callee 0x%p (trampoline 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)callsiteVa, (void*)calleeVa,
               (void*)jit);
    return res;
}

AddResult AddC(const kcdx::hook_payload::HookPayload& payload,
               void*                                  cFn,
               const kcdx::hook_signature::Signature& cSig,
               const std::string& pluginName,
               int                priority,
               const std::string& name,
               uint64_t           handleId) {
    AddResult res;
    if (!cFn) {
        res.reason = "internal: AddC called with null cFn";
        return res;
    }

    // Route mid / callsite WITHOUT touching t_addcEngineStamp — the
    // called function consumes the stamp at its own construction site.
    if (payload.mode == kcdx::hook_payload::Mode::Mid) {
        return AddCMid(payload, cFn, cSig, pluginName, priority, name,
                       handleId);
    }
    if (payload.callsiteScope) {
        return AddCCallsite(payload, cFn, cSig, pluginName, priority, name,
                            handleId);
    }

    if (!payload.hasSignature) {
        res.reason = "internal: C hook has no parsed signature";
        return res;
    }

    // Consume the engine-stamp flag (if AddCEngine set it on this thread).
    // One-shot per call: the next non-engine plugin AddC on this thread
    // sees stamp == false because TakeEngineStamp cleared the flag.
    const bool stamp = TakeEngineStamp();

    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    Chain* chain = FindChain(targetVa);

    if (chain) {
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, cSig,
                        /*incomingIsCallsite=*/false, whyNot)) {
            // Record the loser on the winning chain (same name/priority
            // this entry would have had + the same reason res surfaces),
            // so the conflict report lists it with applied=false. Record
            // first, then move whyNot into res.reason (single source).
            chain->rejected.push_back({name, priority, whyNot});
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.kind           = ChainEntry::Kind::C;
        e.mode           = payload.mode;
        e.cFn            = cFn;
        e.cSig           = cSig;
        e.cDispatchThunk = kcdx::dynamic_call_jit::BuildCDispatchThunk(
            cFn, cSig, payload.mode);
        if (!e.cDispatchThunk) {
            res.reason = "BuildCDispatchThunk failed (see kcdx.log)";
            return res;
        }
        e.pluginName = pluginName;
        e.priority   = priority;
        e.name       = name;
        e.handleId   = handleId;
        e.offThread  = payload.offThread;
        e.isEngine   = stamp;
        const bool needsCallOriginal =
            (payload.mode == kcdx::hook_payload::Mode::Around);
        InsertOrdered(*chain, std::move(e));
        if (needsCallOriginal && !chain->callOriginalCThunk) {
            void** origSlot = chain->rf->get_jit_original_slot();
            if (origSlot && *origSlot) {
                std::string rt; std::vector<std::string> pts;
                SignatureToAbiStrings(chain->sig, rt, pts);
                chain->callOriginalCThunk = (uintptr_t)
                    kcdx::dynamic_call_jit::BuildNativeCallThunk(
                        (uintptr_t)*origSlot, rt, pts);
            }
        }
        res.ok = true;
        log::InfoF("hook_chain: appended C %s '%s' (plugin '%s') to target "
                   "0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)targetVa,
                   chain->entries.size());
        return res;
    }

    auto newChain = std::make_unique<Chain>();
    newChain->targetVa = targetVa;
    newChain->sig      = cSig;
    newChain->rf       = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(cSig, rt, pts);

    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, targetVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    } else {
        // #14 — null call-original slot. The runtime_func_t ctor
        // default-constructs the detour non-null, so this is normally
        // unreachable; but a null slot means the JIT trampoline's
        // call-original path reads null and any around / auto-mid that rets
        // into it jumps to 0 → crash. Pre-Batch-E this silently proceeded and
        // reported install SUCCESS (the two AddCallsite variants already failed
        // the install here; the others did not). Make them CONSISTENT: fail the
        // install with a reason, exactly as AddCallsite does — Error-class
        // (a later around/auto-mid on this target would deref null and crash).
        res.reason = "internal: runtime_func_t has no call-original slot "
                     "(detour_hook missing) — a later around/auto-mid on this "
                     "target would deref null and crash; install aborted";
        return res;
    }

    if (payload.mode == kcdx::hook_payload::Mode::Around) {
        if (install.pOriginal) {
            newChain->callOriginalCThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildNativeCallThunk(
                    (uintptr_t)install.pOriginal, rt, pts);
        }
    }

    ChainEntry e;
    e.kind           = ChainEntry::Kind::C;
    e.mode           = payload.mode;
    e.cFn            = cFn;
    e.cSig           = cSig;
    e.cDispatchThunk = kcdx::dynamic_call_jit::BuildCDispatchThunk(
        cFn, cSig, payload.mode);
    if (!e.cDispatchThunk) {
        res.reason = "BuildCDispatchThunk failed (see kcdx.log)";
        return res;
    }
    e.pluginName = pluginName;
    e.priority   = priority;
    e.name       = name;
    e.handleId   = handleId;
    e.offThread  = payload.offThread;
    e.isEngine   = stamp;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed C %s '%s' (plugin '%s') at target 0x%p "
               "(JIT detour 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)targetVa, (void*)jit);
    return res;
}

AddResult AddCEngine(const kcdx::hook_payload::HookPayload& payload,
                     void*                                  cFn,
                     const kcdx::hook_signature::Signature& cSig,
                     const std::string&                     pluginName,
                     int                                    priority,
                     const std::string&                     name,
                     uint64_t                               handleId) {
    // Set the thread-local stamp, route through the standard AddC body,
    // let the construction site read the stamp into e.isEngine. The stamp
    // is one-shot: AddC's TakeEngineStamp clears it on read, so a plugin
    // AddC on the same thread immediately after sees stamp=false. The
    // stamp survives the AddC -> AddCMid / AddCCallsite routing too —
    // those branches take the stamp at their own construction sites.
    t_addcEngineStamp = true;
    AddResult r = AddC(payload, cFn, cSig, pluginName, priority, name, handleId);
    // Defensive: clear the flag in case AddC returned before consuming it
    // (e.g. a null-cFn / no-signature early exit). Otherwise the next
    // plugin AddC on this thread would inherit the stamp.
    t_addcEngineStamp = false;
    return r;
}

// Uninstall a previously-Add()'d hook by registry handle id.
//
// Option A (locked design): mark the entry removed, leave the trampoline
// alone for the session. The MinHook detour stays installed; the chain's
// JIT trampoline keeps existing. The dispatchers are robust to the
// resulting state — a function-entry chain with empty entries falls
// through DispatchPre's `chain->entries.empty()` guard at the top of §6
// (returns true → original runs) and DispatchPost's `!entries.empty()`
// guard skips the post loop. A mid chain with midCallbackRef == LUA_NOREF
// falls through MidDispatch's `midCallbackRef == -2` early-return (§6b),
// leaving g_midSkipOriginal clear so the JIT runs the captured instruction.
//
// This sidesteps the race entirely: no g_chains.erase, no MH_RemoveHook
// call, no chain teardown. The next Add on the same target REUSES the
// existing chain (entries vector just grows again). Caller updates the
// registry Entry's status to Status::Removed after this returns true
// (lua_registry::SetStatus).
//
// Idempotent: unknown / already-removed handleId returns true.
bool Uninstall(uint64_t handleId) {
    if (handleId == 0) return true;
    std::lock_guard<std::mutex> lock(g_chainsMu);
    for (auto& kv : g_chains) {
        Chain& chain = *kv.second;

        // Mid path: a v1 mid chain stores its single handle on the Chain
        // itself, not in entries. Match by midHandleId and clear the
        // callback ref so MidDispatch's NOREF guard kicks in (trampoline
        // stays — session lifetime).
        if (chain.isMid && chain.midHandleId == handleId) {
            // Lua-kind mid: release the Lua registry ref so the closure
            // becomes GC-eligible. C-kind mid (chunks 3+): the author
            // owns the C function pointer's lifetime; nothing for the
            // engine to release. Today every mid is Kind::Lua (AddMid
            // is the only construction path); the C branch is the chunk
            // 1 parallel of ChainEntry's Lua-vs-C split.
            if (chain.midKind == ChainEntry::Kind::Lua) {
                lua_State* L = g_L;
                if (L && chain.midCallbackRef != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, chain.midCallbackRef);
                } else if (!L && chain.midCallbackRef != LUA_NOREF) {
                    // Defensive: shouldn't fire in practice (Uninstall is
                    // reachable only via a Lua handle, which requires the
                    // VM up + g_L bound at first-tick). If it ever does,
                    // the ref leaks into Lua's registry table — log loudly
                    // so it's discoverable rather than silenced.
                    log::WarnF("hook_chain: Uninstall(%llu) mid: no lua_State "
                               "for unref (ref=%d leaked)",
                               (unsigned long long)handleId,
                               chain.midCallbackRef);
                }
                chain.midCallbackRef = LUA_NOREF;
            } else {
                // C-kind mid: no Lua ref to release. Null the cFn so
                // the chunk-3 MidDispatch C branch's eventual nullptr
                // guard makes a drained C mid a no-op shim (parallel
                // shape to the Lua NOREF guard).
                chain.midCFn = nullptr;
            }
            chain.midHandleId    = 0;
            log::InfoF("hook_chain: Uninstall(%llu) mid OK (chain at %p; "
                       "trampoline retained — session lifetime)",
                       (unsigned long long)handleId, (void*)kv.first);
            return true;
        }

        // Signature / callsite path: find the matching ChainEntry by
        // handleId and erase it. chain.entries.empty() is already handled
        // by DispatchPre/Post (return true → original runs). Trampoline
        // stays — session lifetime.
        for (auto it = chain.entries.begin();
             it != chain.entries.end(); ++it) {
            if (it->handleId != handleId) continue;
            // Lua-kind: release the Lua registry ref. C-kind (chunks 3+):
            // the author owns the C function pointer's lifetime; nothing
            // for the engine to release. Today every entry is Kind::Lua
            // (Add / AddCallsite are the only construction paths); the
            // C branch is the parallel of the mid Uninstall split above.
            if (it->kind == ChainEntry::Kind::Lua) {
                lua_State* L = g_L;
                if (L && it->callbackRef != LUA_NOREF) {
                    luaL_unref(L, LUA_REGISTRYINDEX, it->callbackRef);
                } else if (!L && it->callbackRef != LUA_NOREF) {
                    // Defensive — see the mid branch above.
                    log::WarnF("hook_chain: Uninstall(%llu): no lua_State for "
                               "unref (ref=%d leaked)",
                               (unsigned long long)handleId, it->callbackRef);
                }
            }
            const size_t remaining = chain.entries.size() - 1;
            chain.entries.erase(it);
            log::InfoF("hook_chain: Uninstall(%llu) OK (chain at %p; %zu "
                       "entr%s remaining; trampoline retained — session "
                       "lifetime)",
                       (unsigned long long)handleId, (void*)kv.first,
                       remaining, remaining == 1 ? "y" : "ies");
            return true;
        }
    }
    return true;  // idempotent: unknown id is not an error
}

// All kcdx.hook participants (winners + CanCoexist-rejected losers) at a
// resolved runtime target VA. See the header for VA-space + name-lifetime
// + locking contract. Empty when no kcdx.hook ever touched this VA.
std::vector<ConflictParticipant> GetParticipantsAtTarget(uintptr_t targetVa) {
    std::vector<ConflictParticipant> out;
    std::lock_guard<std::mutex> lock(g_chainsMu);
    Chain* chain = FindChain(targetVa);
    if (!chain) return out;  // no kcdx.hook here — caller's legacy loops still run

    out.reserve(chain->entries.size() + chain->rejected.size());
    // Winners — the live, installed chain entries (applied=true). These
    // are the signature/callsite entries the dispatchers walk. (A mid
    // chain keeps its single callback on Chain itself, not in entries,
    // and a mid never populates `rejected` — mid conflicts reject via the
    // FindChain-non-null path, not CanCoexist — so a mid VA reports no
    // hook_chain participants here. That is consistent with this module's
    // contract: winners come from entries, losers from rejected.)
    for (const auto& e : chain->entries) {
        out.push_back({e.name.c_str(), e.priority, /*applied=*/true});
    }
    // Losers — the CanCoexist-rejected entries (applied=false).
    for (const auto& r : chain->rejected) {
        out.push_back({r.name.c_str(), r.priority, /*applied=*/false});
    }
    return out;
}

std::vector<ChainTarget> GetAllChainTargets() {
    std::vector<ChainTarget> out;
    std::lock_guard<std::mutex> lock(g_chainsMu);
    out.reserve(g_chains.size());
    for (const auto& kv : g_chains) {
        const Chain& chain = *kv.second;
        // Owner attribution: a mid chain keeps its single callback on the
        // Chain itself (midPluginName / midName); a function-entry / callsite
        // chain keeps an ordered entries vector — the FIRST entry is the
        // representative owner (load-order-first; the chain-share / coexist
        // policy means all entries share the target, and the first is the one
        // that fixed the thunk). An empty entries vector (all uninstalled —
        // the detour stays a no-op shim) has no owner; report "".
        const char* pluginName = "";
        const char* hookName   = "";
        if (chain.isMid) {
            pluginName = chain.midPluginName.c_str();
            hookName   = chain.midName.c_str();
        } else if (!chain.entries.empty()) {
            pluginName = chain.entries.front().pluginName.c_str();
            hookName   = chain.entries.front().name.c_str();
        }
        out.push_back({kv.first, pluginName, hookName});  // key IS the VA
    }
    return out;
}

}  // namespace kcdx::hook_chain

// Public surface (declared in hook_signature.h) — forwards to the
// hook_chain impl that keys on this TU's JIT type-string table. Lets the
// named-target install surfaces (hook_interface.cpp / lua_bind_hook.cpp)
// cross-check an explicit author signature against a verified ABI without
// pulling the SigTypeToJitString map into a header (minimal-blast).
namespace kcdx::hook_signature {

bool SignaturesCompatible(const Signature& a, const Signature& b) {
    return kcdx::hook_chain::SignaturesCompatibleImpl(a, b);
}

namespace {

// Return-register WIDTH bucket for a parsed type, in BYTES. Type carries no
// width member, so this is the gate's definition (see ClassifyConflict /
// hook_signature.h). Used ONLY to decide gate SEVERITY (Hard vs Soft), never
// resolution. Conservative: an unmapped type falls to 8 (the GPR full width),
// which can only ever OVER-classify a difference as Soft-not-Hard within the
// same bucket — and the arg-count delta (the unambiguous Hard case) does not
// depend on this map at all.
//
//   void              -> 0  (no return value; void vs non-void IS a width
//                            delta — a hook expecting a return where there is
//                            none, or vice versa, mis-handles the frame)
//   bool / i8 / u8    -> 1
//   i16 / u16         -> 2
//   f32               -> 4   (xmm0, 32-bit lane)
//   i32 / u32         -> 4
//   f64               -> 8   (xmm0, 64-bit lane)
//   i64 / u64         -> 8
//   ptr / wstr / cstr -> 8   (pointer width)
int ReturnWidthBytes(kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Void: return 0;
        case T::Bool: case T::I8:  case T::U8:  return 1;
        case T::I16:  case T::U16:                return 2;
        case T::F32:  case T::I32: case T::U32:   return 4;
        case T::F64:  case T::I64: case T::U64:   return 8;
        case T::Ptr:  case T::Wstr: case T::Cstr: return 8;
    }
    return 8;  // conservative default (full GPR)
}

// True iff two return types occupy the same-width register lane AND the same
// register CLASS (GPR vs XMM). A float and an integer of equal byte width
// (f32 vs i32, f64 vs i64) return in DIFFERENT registers (xmm0 vs rax), so
// reading one as the other mis-reads the return — that is a Hard (shape)
// delta, not a soft per-slot nuance. Both-float or both-non-float of equal
// width is a Soft difference (same register, same width).
bool SameReturnWidthClass(kcdx::hook_signature::Type a,
                          kcdx::hook_signature::Type b) {
    if (ReturnWidthBytes(a) != ReturnWidthBytes(b)) return false;
    return kcdx::hook_signature::IsFloatType(a) ==
           kcdx::hook_signature::IsFloatType(b);
}

}  // namespace

SignatureConflictKind ClassifyConflict(const Signature& explicitSig,
                                       const Signature& verifiedSig) {
    // None — the gate's own compatibility decision (NOT SignaturesCompatible,
    // which is the chain-share question). If the explicit sig is byte-share
    // compatible with the verified ABI, there is nothing to warn about.
    if (kcdx::hook_chain::SignaturesCompatibleImpl(explicitSig, verifiedSig)) {
        return SignatureConflictKind::None;
    }
    // Hard — a SHAPE delta: arg-count mismatch (the unambiguous case, the
    // cap-38 / 0xC8 crash signature) OR a return-register width/class delta
    // (a mis-described return register). Either mis-describes the call frame
    // on a live engine function — a real crash risk.
    if (explicitSig.args.size() != verifiedSig.args.size()) {
        return SignatureConflictKind::Hard;
    }
    if (!SameReturnWidthClass(explicitSig.returnType, verifiedSig.returnType)) {
        return SignatureConflictKind::Hard;
    }
    // Soft — incompatible (per-slot type nuance) but SAME shape: same arg
    // count, same return width/class. A value-level mis-marshal, not a frame
    // mis-description.
    return SignatureConflictKind::Soft;
}

}  // namespace kcdx::hook_signature
