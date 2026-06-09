// plugin_pdb — PDB auto-load worker. See plugin_pdb.h for the contract.
//
// At C++ plugin load, parse the plugin DLL's sidecar .pdb via DbgHelp and
// populate every internal (non-exported) FUNCTION's address into the
// kcdx.functions["<author>.<plugin>"] namespace, so a static op / a hook by name
// on an undeclared internal resolves its address with zero author friction.
//
// The verified DbgHelp sequence that surfaces a deployed DLL's internals:
//   SymInitialize(proc, nullptr, FALSE)
//   SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEBUG)
//   SymLoadModuleEx(proc, nullptr, dllPath, nullptr, base, imageSize, ...)
//   SymEnumSymbols(proc, base, "*", cb, ctx)
//   SymUnloadModule64 / SymCleanup
// The enumerate yields BOTH the plugin's own functions AND CRT/linker privates;
// the in-range + FUNCTION filter (below) isolates the plugin's own functions.
//
// THE LOAD-BEARING CONSTRAINT: internal auto-load works ONLY with a /DEBUG:FULL
// (self-contained) PDB. A FASTLINK PDB (the VS2017+ default) is a build-machine-
// OBJ-indexing stub — SymLoadModuleEx SUCCEEDS but the enumerate yields ZERO of
// the plugin's own functions when deployed. The three-case fallback below
// distinguishes no-PDB / load-fail / loaded-but-stub, each with its own teaching
// log line, so an author whose FULL PDB silently degraded to FASTLINK is told
// exactly that.

#include "plugin_pdb.h"

#include <psapi.h>      // GetModuleInformation (image base + size)
#include <dbghelp.h>    // SymInitialize / SymLoadModuleEx / SymEnumSymbols
#pragma comment(lib, "dbghelp.lib")  // crash_guard.cpp already links it.

#include <cstdint>
#include <string>

#include "lua_bind_functions.h"  // RecordPluginAddress — the address store seam
#include "log.h"                 // LOG_*_KV, ::kcdx::log::KV

namespace kcdx::plugin_pdb {

namespace {

// Input-validation caps (input-validation.md — the plugin DLL + its PDB are an
// external-authored on-disk artifact crossing a trust boundary; SymEnumSymbols
// enumerates whatever the PDB claims). Bound the work and the keys:
//   - kMaxFunctions: never populate an unbounded count from a malformed/huge
//     PDB (a PDB claiming millions of functions must not OOM the map). Past the
//     cap we stop recording and WARN. 64 KiB internal functions is far beyond
//     any real plugin; a PDB exceeding it is malformed/hostile.
//   - kMaxNameLen: a symbol name longer than this is rejected (a sane upper
//     bound for a C++ mangled name; an over-long name is a malformed record).
constexpr uint32_t kMaxFunctions = 65536;
constexpr size_t   kMaxNameLen   = 4096;

// The enumeration callback's context: the module's loaded address range (the
// in-range filter) + the namespace to record under + running counts.
struct EnumCtx {
    DWORD64     base = 0;          // module load base.
    DWORD64     end = 0;           // base + image size (exclusive upper bound).
    std::string ns;               // "<author>.<plugin>" — the record namespace.
    uint32_t    inRangeFuncs = 0;  // plugin's own functions recorded.
    uint32_t    capDropped = 0;    // functions skipped after hitting kMaxFunctions.
    uint32_t    badName = 0;       // symbols rejected on name validation.
};

// Validate a symbol name before it becomes a map key (input-validation.md —
// "validate the content, not just the structure"): non-empty, under the length
// cap, no interior NUL. SymEnumSymbols hands a NUL-terminated Name + a NameLen;
// a name with an embedded NUL or an over-long name is a malformed record we
// reject rather than key the store on.
bool ValidSymbolName(const SYMBOL_INFO* sym) {
    if (!sym || sym->NameLen == 0) return false;
    if (sym->NameLen > kMaxNameLen) return false;
    // Name is the inline char[]; NameLen is the length WITHOUT the terminator.
    // Reject an interior NUL (the declared length must match the C-string) —
    // a manual scan over [0, NameLen) avoids relying on a bounded-strlen that
    // is not in the standard library.
    for (ULONG i = 0; i < sym->NameLen; ++i) {
        if (sym->Name[i] == '\0') return false;
    }
    return true;
}

// SymEnumSymbols callback. Records ONE plugin-own FUNCTION per accepted symbol.
// The two-part filter that isolates the plugin's own functions from the CRT/
// linker privates the enumerate also yields:
//   1. In-range: sym->Address ∈ [base, base+imageSize). A symbol outside this
//      module's own image is a CRT private pulled from the CRT's PDBs / lives
//      elsewhere — reject (input-validation.md: an address outside the module's
//      range is rejected).
//   2. FUNCTION: SYMFLAG_FUNCTION set (or Tag == SymTagFunction). A "*" enumerate
//      yields mostly CRT DATA privates; the plugin's own hookable target is an
//      in-range function. Data symbols are not hookable targets — reject.
BOOL CALLBACK EnumCb(PSYMBOL_INFO sym, ULONG /*symSize*/, PVOID userCtx) {
    auto* ctx = static_cast<EnumCtx*>(userCtx);

    // (1) in-range filter — reject anything outside THIS module's image.
    if (sym->Address < ctx->base || sym->Address >= ctx->end) {
        return TRUE;  // keep enumerating; this is a foreign/CRT symbol.
    }
    // (2) function filter — SYMFLAG_FUNCTION is the documented flag DbgHelp
    // sets on a function symbol; the in-range DATA privates (CRT statics etc.)
    // the enumerate also yields do not carry it, so this isolates the plugin's
    // own functions. (SymTagFunction lives in <cvconst.h>, which <dbghelp.h>
    // does not pull in; the flag is the portable, documented check.)
    if ((sym->Flags & SYMFLAG_FUNCTION) == 0) {
        return TRUE;  // an in-range DATA private — not a hookable target.
    }
    if (!ValidSymbolName(sym)) {
        ++ctx->badName;
        return TRUE;
    }
    // Cap the work — a malformed/huge PDB must not grow the map unbounded.
    if (ctx->inRangeFuncs >= kMaxFunctions) {
        ++ctx->capDropped;
        return TRUE;  // keep counting drops for the WARN, but record no more.
    }

    ::kcdx::lua_bind_functions::RecordPluginAddress(
        ctx->ns, std::string(sym->Name, sym->NameLen),
        static_cast<uintptr_t>(sym->Address));
    ++ctx->inRangeFuncs;
    return TRUE;
}

}  // namespace

void PopulateFromPdb(HMODULE module, const std::string& dllPath,
                     const std::string& pluginNamespace) {
    if (!module || dllPath.empty() || pluginNamespace.empty()) {
        LOG_WARN_KV("PDB", "PDB auto-load skipped — missing module/path/namespace",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("dll", dllPath));
        return;
    }

    const HANDLE proc = GetCurrentProcess();

    // Image base + size (psapi) — SymLoadModuleEx needs the size; the in-range
    // filter needs base + size for its [base, base+imageSize) bound.
    MODULEINFO mi{};
    if (!GetModuleInformation(proc, module, &mi, sizeof(mi))) {
        LOG_WARN_KV("PDB", "PDB auto-load skipped — GetModuleInformation failed",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("dll", dllPath),
                    ::kcdx::log::KV("err",
                        static_cast<unsigned long long>(GetLastError())));
        return;
    }
    const DWORD64 base = reinterpret_cast<DWORD64>(mi.lpBaseOfDll);
    const DWORD imageSize = mi.SizeOfImage;

    // SymInitialize with fInvadeProcess=FALSE: do NOT auto-enumerate every loaded
    // module's symbols (we load exactly the one plugin DLL ourselves).
    if (!SymInitialize(proc, nullptr, FALSE)) {
        LOG_WARN_KV("PDB", "PDB auto-load skipped — SymInitialize failed",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("err",
                        static_cast<unsigned long long>(GetLastError())));
        return;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEBUG);

    // SymLoadModuleEx reads the sidecar .pdb from beside dllPath. SUCCESS here
    // does NOT mean the PDB carries the plugin's internals — a FASTLINK stub
    // also loads. The enumerate count below is the real discriminator.
    const DWORD64 loaded = SymLoadModuleEx(
        proc, nullptr, dllPath.c_str(), nullptr, base, imageSize, nullptr, 0);
    if (loaded == 0) {
        const DWORD err = GetLastError();
        SymCleanup(proc);
        if (err == ERROR_SUCCESS) {
            // Documented DbgHelp idiom: a 0 return with GetLastError()==0 means
            // the module was already loaded — not a failure, but we cannot
            // re-drive the enumerate against an unknown prior load state.
            LOG_INFO_KV("PDB", "PDB auto-load: module already loaded in the "
                        "symbol handler; internal-function auto-load skipped "
                        "(declared + exports still resolve)",
                        ::kcdx::log::KV("namespace", pluginNamespace),
                        ::kcdx::log::KV("dll", dllPath));
            return;
        }
        // No .pdb beside the DLL, OR a present-but-unloadable / GUID-age
        // mismatched .pdb. ERROR_FILE_NOT_FOUND distinguishes the two for a
        // teaching line: no PDB at all vs a PDB that did not match its DLL.
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            LOG_INFO_KV("PDB", "no PDB beside plugin DLL; internal-function "
                        "auto-load unavailable, using exports + declared functions",
                        ::kcdx::log::KV("namespace", pluginNamespace),
                        ::kcdx::log::KV("dll", dllPath));
        } else {
            LOG_WARN_KV("PDB", "PDB for plugin doesn't match its DLL (load "
                        "failed / GUID-age mismatch); falling back to exports + "
                        "declared functions",
                        ::kcdx::log::KV("namespace", pluginNamespace),
                        ::kcdx::log::KV("dll", dllPath),
                        ::kcdx::log::KV("err",
                            static_cast<unsigned long long>(err)));
        }
        return;
    }

    // Enumerate. The in-range + FUNCTION filter (EnumCb) records only the
    // plugin's own functions; everything else (CRT/linker privates, out-of-range
    // symbols, data) is skipped.
    EnumCtx ctx;
    ctx.base = base;
    ctx.end = base + imageSize;
    ctx.ns = pluginNamespace;
    const BOOL enumOk = SymEnumSymbols(proc, base, "*", EnumCb, &ctx);
    const DWORD enumErr = enumOk ? ERROR_SUCCESS : GetLastError();

    SymUnloadModule64(proc, base);
    SymCleanup(proc);

    if (!enumOk) {
        LOG_WARN_KV("PDB", "PDB loaded but symbol enumeration failed; falling "
                    "back to exports + declared functions",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("dll", dllPath),
                    ::kcdx::log::KV("err",
                        static_cast<unsigned long long>(enumErr)));
        return;
    }

    if (ctx.inRangeFuncs == 0) {
        // THE FASTLINK/STUB CASE: the PDB
        // loaded but carries NONE of the plugin's own functions when deployed —
        // a /DEBUG:FASTLINK stub indexes the build-machine OBJs, not a
        // self-contained copy. Tell the author exactly that — NOT the generic
        // "no PDB" line; their PDB loaded, it was the wrong KIND.
        LOG_WARN_KV("PDB", "plugin ships a FASTLINK PDB; rebuild with "
                    "/DEBUG:FULL for internal-function auto-load; falling back "
                    "to exports + declared functions",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("dll", dllPath));
        return;
    }

    // Success — a /DEBUG:FULL PDB surfaced the plugin's own internals. One info
    // line per the populated lifecycle event (logging.md).
    LOG_INFO_KV("PDB", "PDB auto-load populated internal-function addresses",
                ::kcdx::log::KV("namespace", pluginNamespace),
                ::kcdx::log::KV("dll", dllPath),
                ::kcdx::log::KV("functions",
                    static_cast<unsigned long long>(ctx.inRangeFuncs)),
                ::kcdx::log::KV("dropped_over_cap",
                    static_cast<unsigned long long>(ctx.capDropped)),
                ::kcdx::log::KV("rejected_bad_name",
                    static_cast<unsigned long long>(ctx.badName)));

    if (ctx.capDropped > 0) {
        LOG_WARN_KV("PDB", "PDB function count exceeded the cap; some "
                    "internal-function addresses were not recorded",
                    ::kcdx::log::KV("namespace", pluginNamespace),
                    ::kcdx::log::KV("cap",
                        static_cast<unsigned long long>(kMaxFunctions)),
                    ::kcdx::log::KV("dropped",
                        static_cast<unsigned long long>(ctx.capDropped)));
    }
}

}  // namespace kcdx::plugin_pdb
