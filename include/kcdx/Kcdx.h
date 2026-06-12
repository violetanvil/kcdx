// kcdx/Kcdx.h — header-only ergonomic wrapper over the kcdx C++ ABI.
//
// This is the EMPOWERED floor on top of include/kcdx/Interfaces.h. The
// raw interfaces (Interfaces.h) are the always-available floor — every
// capability is reachable through `api->QueryInterface(...)` without ever
// including this header. Kcdx.h layers three conveniences:
//
//   1. `struct Kcdx` — one Init() call fetches every shipped sub-interface
//      and stashes the plugin's identity (handle + author/plugin names), so
//      the empowered helpers thread `owningPlugin` for you automatically.
//
//   2. `namespace kcdx::hook` — typed templated install helpers
//      (Before<Sig> / After<Sig> / Around<Sig> / Replace<Sig> and their
//      Try* variants). The author writes a NATURAL callback typed in the
//      ORIGINAL target's signature; this header emits the per-mode adapter
//      that unpacks the engine's JIT-thunk ABI into typed params, invokes
//      the author's callable, and writes back. The author never hand-writes
//      `uintptr_t args[], int* outCount` or the per-mode mangled cFn shape
//      — that mangling is the engine's heavy lifting (the engine carries
//      address AND ABI), and hiding it is the entire point of this header.
//
//   3. `namespace kcdx::bytes` — `Write(K, target, replacement)` /
//      `TryWrite(...)` helpers over `kcdxBytesInterface::Register`. The
//      author supplies a name and a replacement string positionally; the
//      wrapper builds the options struct, threads `owningPlugin = K.self`,
//      and auto-logs on a zero handle. A byte rewrite has no callback to
//      adapt (the engine writes bytes; no per-mode codegen is needed), so
//      the bytes wrapper is shape-simpler than the hook wrapper.
//
// THE 3-FLOOR MODEL (full reference: docs/cpp/wrapper.md):
//
//   Floor 1 (empowered)  kcdx::hook::Before<Sig, &fn>(K, target)
//                         — typed callback, auto-threaded owningPlugin,
//                           auto-log-on-failure. The everyday path.
//   Floor 2 (Try*)       kcdx::hook::TryBefore<Sig, &fn>(K, target)
//                         — same codegen, returns the handle instead of
//                           void+log (programmatic-branch cases).
//   Floor 4 (raw)        K.hook->Before(target, (void*)&cFn, &opts)
//                         — the raw kcdxHookInterface. `K.hook` IS the
//                           unchecked drop-down. Mid / Callsite have no
//                           empowered helper (expert sub-verbs):
//                           the author drops to K.hook->Mid / ->Callsite.
//
//   (There is no Floor 3 "InstallRawUnchecked" — `K.hook` is the unchecked
//    floor by construction. void* callback is unchecked already.)
//
// Author callables must be NON-CAPTURING (a free function or a captureless
// lambda — both convert to a plain function pointer). This is the SAME
// constraint the raw floor states ("capturing lambdas are NOT directly
// callable" — Interfaces.h kcdxHookInterface doc) — not a new limitation;
// to carry state, pass a free function that reads into your own static.
//
// C++17, header-only, zero new globals.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kcdx/Interfaces.h"

// =============================================================================
// struct Kcdx — fetch-once handle to the kcdx surface + stashed identity
// =============================================================================
//
// Build one at kcdxPlugin_Load:
//
//   static Kcdx K;
//   void my_before(int& seed) { seed += 1; }   // NATURAL by-ref callback
//   bool kcdxPlugin_Load(const kcdxInterface* api) {
//       if (!K.Init(api, "redmoon", "outfit")) return true;  // logs why
//       kcdx::hook::Before<int(int), &my_before>(K, "IsInCombat");
//       return true;
//   }
//
// The author's callable is a NON-TYPE TEMPLATE PARAMETER (a function pointer),
// not a runtime argument — this is what lets the generated adapter be a
// non-capturing static whose ADDRESS is the void* the engine receives (the
// SKSE trampoline-generation idiom). A C++17 captureless lambda works too
// when named: `static constexpr auto f = +[](int& s){ s += 1; };
// Before<int(int), f>(K, "IsInCombat");`
//
// Init fetches the shipped QueryInterface set (Hook / Bytes / Memory / Console /
// Trampoline / Messaging / Task / Scripting / Serialization), resolves the
// plugin's own handle via api->GetPluginHandle(plugin) (the BARE [plugin].name,
// matching plugin_loader's FindByName), and builds K.log from that handle.
//
// Address Library + test reporting are NOT separate interfaces — they live on
// the ROOT kcdxInterface. Reach them via K.api:
//   K.api->ResolveAddress(id) / ResolveAddressByNameAs(K.self, name)
//   K.api->ReportTestResult(K.self, row, pass, reason)
struct Kcdx {
    // Raw root interface — the floor-4 drop-down for everything not on a
    // typed field below (ResolveAddress*, ReportTestResult, GetPluginInfo,
    // EnumeratePlugins, GetConflictReport, ResolveSymbol*, Log).
    const kcdxInterface* api = nullptr;

    // Stashed identity. `self` is the handle the
    // empowered helpers thread into opts.owningPlugin so the self-tier of
    // self > engine > other resolves the calling plugin.
    kcdxPluginHandle self   = kcdxInvalidPluginHandle;
    const char*      author = nullptr;  // [plugin].author (stamped, not yet
    const char*      plugin = nullptr;  // [plugin].name    a resolver input)

    // Shipped sub-interface pointers (null if the running engine doesn't
    // implement that interface/version).
    const kcdxHookInterface*          hook         = nullptr;  // floor-4 hook drop-down
    const kcdxBytesInterface*         bytes        = nullptr;  // kcdx.bytes peer
    const kcdxDeclareInterface*       declare      = nullptr;  // kcdx.declare / kcdx.declared peer
    const kcdxFunctionsInterface*     functions    = nullptr;  // kcdx.functions.* peer (function refs)
    const kcdxDllInterface*           dll          = nullptr;  // kcdx.dll.declare peer (declare own DLL fns)
    const kcdxStatementInterface*     statement    = nullptr;  // kcdx.statement.* peer (static-bytes modification)
    const kcdxBehaviorInterface*      behavior     = nullptr;  // kcdx.behavior.* peer (named behaviors + value handles)
    const kcdxAssetInterface*         assets       = nullptr;  // kcdx.assets.* peer
    const kcdxMemoryInterface*        memory       = nullptr;
    const kcdxConsoleInterface*       console      = nullptr;
    const kcdxTrampolineInterface*    code         = nullptr;  // kcdx.code peer
    kcdxMessagingInterface*           messaging    = nullptr;
    kcdxTaskInterface*                task         = nullptr;
    kcdxScriptingInterface*           scripting    = nullptr;
    kcdxSerializationInterface*       serialization= nullptr;

    // Ergonomic logger, stamped with `self`.
    kcdxLogger log;

    // Fetch every shipped sub-interface; resolve identity; build the logger.
    // `authorName` / `pluginName` are the [plugin].author / [plugin].name from
    // your manifest. Returns false (after logging the reason) only when a
    // REQUIRED interface is missing — at minimum Hook (the wrapper's reason to
    // exist). The optional sub-interfaces are best-effort: a null field means
    // "not available in this engine," same as a raw QueryInterface miss.
    bool Init(const kcdxInterface* a, const char* authorName,
              const char* pluginName) {
        api    = a;
        author = authorName;
        plugin = pluginName;
        if (!api) return false;

        self = api->GetPluginHandle(pluginName);
        log  = kcdxLogger(api, self);

        hook = static_cast<const kcdxHookInterface*>(
            api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
        bytes = static_cast<const kcdxBytesInterface*>(
            api->QueryInterface(kcdxInterface_Bytes, kcdxBytesInterface_Version));
        declare = static_cast<const kcdxDeclareInterface*>(
            api->QueryInterface(kcdxInterface_Declare,
                                kcdxDeclareInterface_Version));
        functions = static_cast<const kcdxFunctionsInterface*>(
            api->QueryInterface(kcdxInterface_Functions,
                                kcdxFunctionsInterface_Version));
        dll = static_cast<const kcdxDllInterface*>(
            api->QueryInterface(kcdxInterface_Dll, kcdxDllInterface_Version));
        statement = static_cast<const kcdxStatementInterface*>(
            api->QueryInterface(kcdxInterface_Statement,
                                kcdxStatementInterface_Version));
        behavior = static_cast<const kcdxBehaviorInterface*>(
            api->QueryInterface(kcdxInterface_Behavior,
                                kcdxBehaviorInterface_Version));
        assets = static_cast<const kcdxAssetInterface*>(
            api->QueryInterface(kcdxInterface_Assets,
                                kcdxAssetInterface_Version));
        memory = static_cast<const kcdxMemoryInterface*>(
            api->QueryInterface(kcdxInterface_Memory, kcdxMemoryInterface_Version));
        console = static_cast<const kcdxConsoleInterface*>(
            api->QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version));
        code = static_cast<const kcdxTrampolineInterface*>(
            api->QueryInterface(kcdxInterface_Trampoline,
                                kcdxTrampolineInterface_Version));
        messaging = static_cast<kcdxMessagingInterface*>(
            api->QueryInterface(kcdxInterface_Messaging,
                                kcdxMessagingInterface_Version));
        task = static_cast<kcdxTaskInterface*>(
            api->QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version));
        scripting = static_cast<kcdxScriptingInterface*>(
            api->QueryInterface(kcdxInterface_Scripting,
                                kcdxScriptingInterface_Version));
        serialization = static_cast<kcdxSerializationInterface*>(
            api->QueryInterface(kcdxInterface_Serialization,
                                kcdxSerializationInterface_Version));

        if (!hook) {
            log.Error("KCDX",
                "Kcdx::Init: QueryInterface(Hook, v%u) returned null — the "
                "empowered hook helpers are unavailable (engine version "
                "mismatch?). Returning false.",
                kcdxHookInterface_Version);
            return false;
        }
        return true;
    }
};

namespace kcdx {

// =============================================================================
// sig_traits — decompose R(Args...) into return type + the type→DSL token map
// =============================================================================
//
// C++17 partial specialization (C confirmed: no C++20 concepts). The author
// writes `Before<int(int seed)>(...)`; `Sig = int(int)`. sig_traits<Sig>
// exposes Ret, the arg pack, the arity, and Signature() — the DSL string the
// engine parses (hook_signature.h grammar: void/i8../i64/u8../u64/f32/f64/ptr/
// bool/wstr/cstr; `int` aliases i32).

namespace detail {

// One type → its hook_signature.h DSL token. Default = "ptr" (any pointer or
// unrecognized pointer-width type marshals as a raw slot). Specialized below
// for the scalar primitives the DSL names.
template<class T>
struct dsl_token {
    static constexpr const char* value = "ptr";
};

#define KCDX_DSL_TOKEN(TYPE, TOKEN)                       \
    template<> struct dsl_token<TYPE> {                   \
        static constexpr const char* value = TOKEN;       \
    }

KCDX_DSL_TOKEN(void,               "void");
KCDX_DSL_TOKEN(bool,               "bool");
KCDX_DSL_TOKEN(char,               "i8");
KCDX_DSL_TOKEN(signed char,        "i8");
KCDX_DSL_TOKEN(unsigned char,      "u8");
KCDX_DSL_TOKEN(short,              "i16");
KCDX_DSL_TOKEN(unsigned short,     "u16");
KCDX_DSL_TOKEN(int,                "i32");
KCDX_DSL_TOKEN(unsigned int,       "u32");
KCDX_DSL_TOKEN(long,               "i32");   // LLP64 (MSVC): long is 32-bit
KCDX_DSL_TOKEN(unsigned long,      "u32");
KCDX_DSL_TOKEN(long long,          "i64");
KCDX_DSL_TOKEN(unsigned long long, "u64");
KCDX_DSL_TOKEN(float,              "f32");
KCDX_DSL_TOKEN(double,             "f64");
KCDX_DSL_TOKEN(const char*,        "cstr");
KCDX_DSL_TOKEN(const wchar_t*,     "wstr");

#undef KCDX_DSL_TOKEN

template<class T>
constexpr const char* token() { return dsl_token<T>::value; }

// Append `s` into buf[pos..cap) (NUL-truncating); returns the new position.
inline std::size_t append(char* buf, std::size_t cap, std::size_t pos,
                          const char* s) {
    while (*s && pos + 1 < cap) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

// Write the arg list "(t0, t1, ...)" into buf. Empty arg pack → "()".
template<class... Args>
inline void write_args(char* buf, std::size_t cap, std::size_t& pos) {
    pos = append(buf, cap, pos, "(");
    const char* toks[] = { token<Args>()..., nullptr };
    (void)toks;
    bool first = true;
    for (std::size_t i = 0; i < sizeof...(Args); ++i) {
        if (!first) pos = append(buf, cap, pos, ", ");
        pos = append(buf, cap, pos, toks[i]);
        first = false;
    }
    pos = append(buf, cap, pos, ")");
}

}  // namespace detail

template<class Sig>
struct sig_traits;  // primary left undefined — only function types specialize.

template<class R, class... Args>
struct sig_traits<R(Args...)> {
    using Ret = R;
    static constexpr std::size_t arity = sizeof...(Args);

    // The DSL string: "<ret> (<arg0>, <arg1>, ...)". Rendered into a caller-
    // owned buffer (no heap; header-only). Threaded into opts.signature ONLY
    // on the no-name path (see hook::detail::Install) — a named target carries
    // its verified ABI and this string is left null (disassembler test, B2).
    static void Signature(char* buf, std::size_t cap) {
        std::size_t pos = 0;
        pos = detail::append(buf, cap, pos, detail::token<R>());
        pos = detail::append(buf, cap, pos, " ");
        detail::write_args<Args...>(buf, cap, pos);
    }
};

namespace hook {

// =============================================================================
// Per-mode adapters — natural author callable -> the engine's JIT-thunk ABI
// =============================================================================
//
// Each adapter is a STATIC, NON-CAPTURING function templated on (Sig, the
// author's function-pointer type, and the author's function pointer as a
// non-type template parameter). It has the exact per-mode cFn ABI the JIT
// thunk (src/dynamic_call_jit.cpp::BuildCDispatchThunk) expects; inside, it
// unpacks the slots into typed params, invokes the author's callable, and
// writes back. Address-of one of these adapters is what we hand the engine as
// the void* callback — so the author's natural function is the only thing
// they write.
//
// Author-facing natural shapes (Sig = R(Args...)):
//   Before:  void(Args&...)            — mutate args in place; adapter writes
//                                         the mutated slots back + sets outCount
//   After:   R(R origReturn, Args...)  — non-void R; return the new R
//            void(Args...)             — void R; observe only
//   Around:  R(R(*call_original)(Args...), Args...) — call_original is a plain
//                                         typed fn ptr; return R
//   Replace: R(Args...)                — original never runs; return R

namespace detail {

// ---- BEFORE -----------------------------------------------------------------
// Engine ABI: void cFn(uintptr_t args[], int* outCount, Args... typedArgs).
// The typed trailing args are the engine's read-only view; the AUTHORITATIVE
// mutation channel is args[]/outCount. The adapter binds each slot to a local
// of the matching type, hands the author by-reference locals, then writes each
// (possibly-mutated) local back to args[i] and commits all of them via
// *outCount = arity.
template<class Sig> struct before_adapter;

template<class R, class... Args>
struct before_adapter<R(Args...)> {
    using Fn = void(*)(Args&...);

    template<Fn fn>
    static void Run(uintptr_t args[], int* outCount, Args... typedArgs) {
        // Mutable locals seeded from the engine's typed pass-through values
        // (the engine pre-populates args[] with the same slot values —
        // dynamic_call_jit.cpp:601-611 — so the typed view is equivalent and
        // type-correct). The author mutates these by-reference; we write each
        // back to its 8-byte slot afterward and commit all of them.
        std::tuple<Args...> locals{ typedArgs... };
        Invoke<fn>(locals, std::index_sequence_for<Args...>{});
        WriteBack(args, locals, std::index_sequence_for<Args...>{});
        if (outCount) *outCount = static_cast<int>(sizeof...(Args));
    }

  private:
    template<Fn fn, std::size_t... I>
    static void Invoke(std::tuple<Args...>& locals, std::index_sequence<I...>) {
        fn(std::get<I>(locals)...);
    }

    template<std::size_t... I>
    static void WriteBack(uintptr_t args[], std::tuple<Args...>& locals,
                          std::index_sequence<I...>) {
        int dummy[] = { 0, (SlotWrite(args[I], std::get<I>(locals)), 0)... };
        (void)dummy;
    }

    // Pack a (possibly-mutated) arg value into the 8-byte slot the engine
    // reads back into the param register.
    template<class T>
    static void SlotWrite(uintptr_t& slot, const T& v) {
        static_assert(sizeof(T) <= sizeof(uintptr_t),
                      "Before arg wider than a register slot is not supported "
                      "by the wrapper; drop to the raw K.hook->Before floor.");
        slot = 0;
        std::memcpy(&slot, &v, sizeof(T));
    }
};

// ---- AFTER ------------------------------------------------------------------
// Non-void R: engine ABI is R cFn(R origReturn, Args...); author returns new R.
// Void R: engine ABI is void cFn(Args...); author observes only.
template<class Sig> struct after_adapter;

template<class R, class... Args>
struct after_adapter<R(Args...)> {
    using Fn = R(*)(R, Args...);          // non-void shape

    template<Fn fn>
    static R Run(R origReturn, Args... a) {
        return fn(origReturn, a...);
    }
};

template<class... Args>
struct after_adapter<void(Args...)> {
    using Fn = void(*)(Args...);          // void shape — no origReturn

    template<Fn fn>
    static void Run(Args... a) {
        fn(a...);
    }
};

// ---- AROUND -----------------------------------------------------------------
// Engine ABI: R cFn(R(*call_original)(Args...), Args...). The engine passes
// call_original as a pointer-width register; we declare it typed so the author
// invokes it with the natural args. Author shape is identical to the engine
// ABI here (call_original first, then args), so the adapter is a pass-through —
// the value of the wrapper for Around is the typed sig_traits derivation + the
// auto-threaded owningPlugin/signature, not arg repacking.
template<class Sig> struct around_adapter;

template<class R, class... Args>
struct around_adapter<R(Args...)> {
    using CallOrig = R(*)(Args...);
    using Fn       = R(*)(CallOrig, Args...);

    template<Fn fn>
    static R Run(CallOrig call_original, Args... a) {
        return fn(call_original, a...);
    }
};

// ---- REPLACE ----------------------------------------------------------------
// Engine ABI: R cFn(Args...). Original never runs. Pass-through.
template<class Sig> struct replace_adapter;

template<class R, class... Args>
struct replace_adapter<R(Args...)> {
    using Fn = R(*)(Args...);

    template<Fn fn>
    static R Run(Args... a) {
        return fn(a...);
    }
};

// =============================================================================
// Install plumbing — thread owningPlugin + (no-name-path-only) signature
// =============================================================================
//
// Both the void+log helpers and the Try* helpers route through Install. It
// takes the per-mode interface method pointer, the adapter address (already
// type-erased to void*), the target, the author's opts (may be null), and a
// rendered signature buffer. It builds a local kcdxHookOptions, copies the
// author's opts if any, stamps owningPlugin = K.self, and — per B2 — sets
// opts.signature to the derived DSL string ONLY when there is NO name (the
// install uses an address/advanced locator). On a named target it leaves
// signature null so the engine substitutes the verified ABI (disassembler
// test). An author-supplied signature is never overwritten.
using Method = kcdxHookHandle (*)(const char*, void*, const kcdxHookOptions*);

// True iff the install carries a NAME that can supply the verified ABI: a
// non-empty positional `target`, or any opts locator that resolves to a named
// library/symbol entry (addressId / targetSymbol / targetLuaCfunction). A raw
// `address` or `pattern` carries no ABI → the wrapper must supply the derived
// signature.
inline bool has_named_abi(const char* target, const kcdxHookOptions* opts) {
    if (target && target[0]) return true;
    if (!opts) return false;
    if (opts->addressId != 0) return true;
    if (opts->targetSymbol && opts->targetSymbol[0]) return true;
    if (opts->targetLuaCfunction && opts->targetLuaCfunction[0]) return true;
    return false;
}

inline kcdxHookHandle Install(const Kcdx& K, Method method, const char* target,
                              void* adapter, const kcdxHookOptions* userOpts,
                              const char* derivedSig) {
    if (!K.hook || !method) {
        K.log.Error("HOOK",
            "kcdx::hook install on a Kcdx with no Hook interface — call "
            "Kcdx::Init (and check its return) before installing hooks");
        return 0;
    }
    kcdxHookOptions opts = userOpts ? *userOpts : kcdxHookOptions{};
    opts.owningPlugin = K.self;

    // B2: supply the derived signature only when no name carries the ABI and
    // the author hasn't already set one. hook_interface.cpp:252-258 takes an
    // explicit signature as the winner and the verified ABI only as a fallback;
    // matching that, we fill signature on the no-name path so the unnamed-locator
    // gate (hook_interface.cpp:266-274) is satisfied.
    if ((!opts.signature || !opts.signature[0]) &&
        !has_named_abi(target, &opts)) {
        opts.signature = derivedSig;
    }
    return method(target, adapter, &opts);
}

}  // namespace detail

// =============================================================================
// Empowered helpers — the everyday floor-1 surface
// =============================================================================
//
// Each takes (const Kcdx& K, const char* target, opts=null). The author's
// callable is the SECOND template parameter (a function pointer), so the
// generated adapter is a non-capturing static whose address is handed to the
// engine. Pass a free function or a named captureless lambda decayed to a
// fn ptr:
//
//   void before(int& seed) { seed += 1; }
//   kcdx::hook::Before<int(int), &before>(K, "IsInCombat");
//
// The void-returning forms auto-log on a zero (failed) handle and return void;
// the Try* forms return the handle for programmatic branching.

// ---- BEFORE -----------------------------------------------------------------
template<class Sig, typename detail::before_adapter<Sig>::Fn Fn>
kcdxHookHandle TryBefore(const Kcdx& K, const char* target,
                         const kcdxHookOptions* opts = nullptr) {
    char sig[256]; sig_traits<Sig>::Signature(sig, sizeof(sig));
    void* adapter = reinterpret_cast<void*>(&detail::before_adapter<Sig>::template Run<Fn>);
    return detail::Install(K, K.hook ? K.hook->Before : nullptr, target, adapter, opts, sig);
}

template<class Sig, typename detail::before_adapter<Sig>::Fn Fn>
void Before(const Kcdx& K, const char* target,
            const kcdxHookOptions* opts = nullptr) {
    kcdxHookHandle h = TryBefore<Sig, Fn>(K, target, opts);
    if (h == 0)
        K.log.Error("HOOK", "kcdx::hook::Before('%s') failed to register "
                    "(handle 0) — see the engine log for the teaching error",
                    target ? target : "<advanced-locator>");
}

// ---- AFTER ------------------------------------------------------------------
template<class Sig, typename detail::after_adapter<Sig>::Fn Fn>
kcdxHookHandle TryAfter(const Kcdx& K, const char* target,
                        const kcdxHookOptions* opts = nullptr) {
    char sig[256]; sig_traits<Sig>::Signature(sig, sizeof(sig));
    void* adapter = reinterpret_cast<void*>(&detail::after_adapter<Sig>::template Run<Fn>);
    return detail::Install(K, K.hook ? K.hook->After : nullptr, target, adapter, opts, sig);
}

template<class Sig, typename detail::after_adapter<Sig>::Fn Fn>
void After(const Kcdx& K, const char* target,
           const kcdxHookOptions* opts = nullptr) {
    kcdxHookHandle h = TryAfter<Sig, Fn>(K, target, opts);
    if (h == 0)
        K.log.Error("HOOK", "kcdx::hook::After('%s') failed to register "
                    "(handle 0) — see the engine log for the teaching error",
                    target ? target : "<advanced-locator>");
}

// ---- AROUND -----------------------------------------------------------------
template<class Sig, typename detail::around_adapter<Sig>::Fn Fn>
kcdxHookHandle TryAround(const Kcdx& K, const char* target,
                         const kcdxHookOptions* opts = nullptr) {
    char sig[256]; sig_traits<Sig>::Signature(sig, sizeof(sig));
    void* adapter = reinterpret_cast<void*>(&detail::around_adapter<Sig>::template Run<Fn>);
    return detail::Install(K, K.hook ? K.hook->Around : nullptr, target, adapter, opts, sig);
}

template<class Sig, typename detail::around_adapter<Sig>::Fn Fn>
void Around(const Kcdx& K, const char* target,
            const kcdxHookOptions* opts = nullptr) {
    kcdxHookHandle h = TryAround<Sig, Fn>(K, target, opts);
    if (h == 0)
        K.log.Error("HOOK", "kcdx::hook::Around('%s') failed to register "
                    "(handle 0) — see the engine log for the teaching error",
                    target ? target : "<advanced-locator>");
}

// ---- REPLACE ----------------------------------------------------------------
template<class Sig, typename detail::replace_adapter<Sig>::Fn Fn>
kcdxHookHandle TryReplace(const Kcdx& K, const char* target,
                          const kcdxHookOptions* opts = nullptr) {
    char sig[256]; sig_traits<Sig>::Signature(sig, sizeof(sig));
    void* adapter = reinterpret_cast<void*>(&detail::replace_adapter<Sig>::template Run<Fn>);
    return detail::Install(K, K.hook ? K.hook->Replace : nullptr, target, adapter, opts, sig);
}

template<class Sig, typename detail::replace_adapter<Sig>::Fn Fn>
void Replace(const Kcdx& K, const char* target,
             const kcdxHookOptions* opts = nullptr) {
    kcdxHookHandle h = TryReplace<Sig, Fn>(K, target, opts);
    if (h == 0)
        K.log.Error("HOOK", "kcdx::hook::Replace('%s') failed to register "
                    "(handle 0) — see the engine log for the teaching error",
                    target ? target : "<advanced-locator>");
}

}  // namespace hook

// =============================================================================
// namespace kcdx::bytes — the empowered floor over kcdxBytesInterface
// =============================================================================
//
// The C++ peer of Lua's `kcdx.bytes{...}` smart-resolver shape. The author
// supplies a name + a replacement string; this wrapper builds the
// kcdxBytesOptions, threads `owningPlugin = K.self`, calls
// `K.bytes->Register(&opts)`, and (for the void+log form) auto-logs on a zero
// handle. No per-mode adapter codegen — a byte rewrite has no callback to
// adapt; the options struct carries `replacement` directly.
//
// Floor model (see docs/cpp/wrapper.md "The 3-floor model"):
//   Floor 1 (empowered)  kcdx::bytes::Write(K, "open_inventory_check", "90 90 90")
//                         — auto-threaded owningPlugin, auto-log on failure.
//   Floor 2 (Try*)       kcdx::bytes::TryWrite(K, target, replacement)
//                         — returns the handle for programmatic branching.
//   Floor 4 (raw)        K.bytes->Register(&opts) — the unchecked raw
//                         kcdxBytesInterface; reach for it for the [advanced]
//                         locator-only paths the wrapper does not pre-fill
//                         positionally (pattern / addressId / targetSymbol).

namespace bytes {

inline kcdxBytesHandle TryWrite(const Kcdx& K, const char* target,
                                const char* replacement,
                                const kcdxBytesOptions* userOpts = nullptr) {
    if (!K.bytes) {
        K.log.Error("BYTES",
            "kcdx::bytes::Write on a Kcdx with no Bytes interface — call "
            "Kcdx::Init (and check its return) before installing byte rewrites");
        return 0;
    }
    kcdxBytesOptions opts = userOpts ? *userOpts : kcdxBytesOptions{};
    opts.owningPlugin = K.self;
    if (target)      opts.target      = target;
    if (replacement) opts.replacement = replacement;
    return K.bytes->Register(&opts);
}

inline void Write(const Kcdx& K, const char* target, const char* replacement,
                  const kcdxBytesOptions* userOpts = nullptr) {
    kcdxBytesHandle h = TryWrite(K, target, replacement, userOpts);
    if (h == 0)
        K.log.Error("BYTES", "kcdx::bytes::Write('%s') failed to register "
                    "(handle 0) — see the engine log for the teaching reason",
                    target ? target : "<advanced-locator>");
}

}  // namespace bytes

// =============================================================================
// namespace kcdx::console — the empowered floor over kcdxConsoleInterface::Print
// =============================================================================
//
// The C++ peer of Lua's `kcdx.console.print(text)`. The author passes one
// plain string; this wrapper null-guards the interface + slot and forwards to
// `K.console->Print(text)`. Unlike the hook / bytes wrappers there is no
// options struct to build and no per-mode codegen — a console print is a single
// one-arg call — so the wrapper's value is the null-guard and the namespace
// symmetry (an author scanning the `kcdx::<domain>::` namespaces finds console
// alongside hook / bytes).
//
// The null-guard is genuine safety: `Print` is the append-only v2 slot on
// kcdxConsoleInterface. A plugin compiled against a newer header but loaded by
// an OLDER engine sees `K.console == nullptr` (the version-mismatched
// QueryInterface returns null) or a null `Print` field — the wrapper returns a
// safe `false` instead of dereferencing a null function pointer. The raw
// `K.console->Print(text)` floor carries no such guard.
//
// Floor model (see docs/cpp/wrapper.md "The 3-floor model"):
//   Floor 1 (empowered)  kcdx::console::print(K, "hello")
//                         — null-guards K.console + ->Print, returns false on a
//                           missing slot instead of crashing.
//   Floor 4 (raw)        K.console->Print("hello") — the unchecked raw
//                         kcdxConsoleInterface slot.

namespace console {

// Print one plain line to the in-game `~` console overlay. The empowered peer
// of K.console->Print — same behavior, the Kcdx-handle-first house shape, with
// a null-guard so an older-engine `Print == nullptr` returns false rather than
// crashing. Returns true if the line was accepted; false if the console
// interface/slot is unavailable, the surface isn't ready, or the print path
// could not be resolved on this game build (the engine logs a refusal — never a
// silent no-op).
inline bool print(const Kcdx& K, const char* text) {
    return (K.console && K.console->Print) ? K.console->Print(text) : false;
}

}  // namespace console
}  // namespace kcdx
