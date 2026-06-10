#include "cvar.h"

#include <atomic>
#include <cstdint>

#include "log.h"
#include "refdb.h"
#include "survival_verify.h"  // RecordKcdxInvocation — the CALLED-by-kcdx rank-1
                              // signal (survival_verify.h pulls in only survival.h;
                              // no cvar/console back-edge, so no include cycle).

namespace kcdx::cvar {

namespace {

// IConsole::GetCVar(const char* name) -> ICVar*. Flat-callable like
// console.cpp's other IConsole accessors: rcx=IConsole*, rdx=const char*.
// Resolved by name (id 16) at Init(); the recorded RVA IS the call target
// (a flat function, unlike the ICVar accessors below which are vtable
// methods dispatched through the runtime object).
using GetCVarFn = void* (__fastcall*)(void* iconsole, const char* name);

// ICVar::GetIVal() -> int  / ICVar::GetFVal() -> float.
// __thiscall via the runtime object's vtable (rcx=ICVar*). On x64 MSVC the
// __thiscall ABI passes `this` in rcx — the same register __fastcall's
// first arg uses — so __fastcall(self) is the correct flat shape for a
// no-other-arg vtable method.
using GetIValFn = int   (__fastcall*)(void* icvar);
using GetFValFn = float (__fastcall*)(void* icvar);

// Resolved surface. Populated by Init().
void*    g_pConsoleStorage = nullptr;   // &gEnv->pConsole (deref each call — the
                                        // console pointer can be null at Init but
                                        // live later; re-read, never cache the
                                        // dereffed value)
GetCVarFn g_GetCVar        = nullptr;

// GetIVal/GetFVal are VTABLE methods, not flat functions. The ICVar* that
// GetCVar returns is any concrete CVar subclass (CXConsoleVariableInt /
// ...Float / ...Ref / ...), each with its OWN vtable; the recorded RVA in
// the DB is just one concrete class's body. We dispatch through the
// runtime object's own vtable at these slot INDICES.
// SOURCE: Address Library — ICVar_GetIVal (id 156, vtable[2]) +
//         ICVar_GetFVal (id 157, vtable[4]); verified + AP18-approved.
//         Read from the DB at Init() (refdb NameResolution::vtable_slot),
//         not hardcoded — a new game version updates the DB row, not source.
int64_t  g_getIValSlot     = -1;
int64_t  g_getFValSlot     = -1;

// The curated ROW VAs for the GetIVal/GetFVal entities — WhgameBase()+rva, the
// SAME VA the startup verification sweep resolves these rows to. These are NOT
// the runtime call target (that is the live object's vtable slot, a concrete
// subclass body); they are the curated row's identity, used ONLY to key the
// CALLED-by-kcdx invocation record so the sweep matches "kcdx invoked the
// GetIVal/GetFVal target + it returned" against the right row. 0 when the DB
// predates the entity (the record is then a no-op for that row — never a wrong
// key). Resolved from refdb at Init() (the same resolution source the sweep
// uses), never hardcoded.
uintptr_t g_getIValRowVa   = 0;
uintptr_t g_getFValRowVa   = 0;

std::atomic<bool> g_ready  {false};

// Dispatch a no-arg ICVar vtable method returning T through the runtime
// object's OWN vtable. INVARIANT: caller guarantees icvar != null and slot
// >= 0. ICVar layout: first qword is the vtable pointer; slot N at
// vtable + N*8.
template <typename Fn>
Fn IcvarSlot(void* icvar, int64_t slot) {
    void** vtable = *reinterpret_cast<void***>(icvar);
    return reinterpret_cast<Fn>(vtable[slot]);
}

// Shared resolve+dispatch path for both GetInt and GetFloat. Returns the
// runtime ICVar* on success (non-null), or null on any miss (surface
// unready, bad name, no such CVar) — having already logged the WARN. The
// caller then dispatches the type-specific vtable slot. *out is never
// touched here.
void* ResolveCVar(const char* name, const char* call) {
    if (!g_ready.load(std::memory_order_acquire)) {
        LOG_WARN_KV("CVAR", "miss",
                    log::KV("call", call),
                    log::KV("reason", "surface_not_ready"));
        return nullptr;
    }
    if (!name || !*name) {
        LOG_WARN_KV("CVAR", "miss",
                    log::KV("call", call),
                    log::KV("reason", "null_or_empty_name"));
        return nullptr;
    }
    // Re-read gEnv->pConsole each call — it is the same IConsole the console
    // surface uses; reading it fresh (not caching the dereffed value)
    // matches console.cpp's storage-deref and survives a console swap.
    void* iconsole = *reinterpret_cast<void**>(g_pConsoleStorage);
    if (!iconsole) {
        LOG_WARN_KV("CVAR", "miss",
                    log::KV("call", call),
                    log::KV("name", name),
                    log::KV("reason", "pconsole_null"));
        return nullptr;
    }
    void* icvar = g_GetCVar(iconsole, name);
    if (!icvar) {
        // A CVar that does not exist — fail loud, return null. The caller
        // returns false WITHOUT writing *out (no garbage).
        LOG_WARN_KV("CVAR", "miss",
                    log::KV("call", call),
                    log::KV("name", name),
                    log::KV("reason", "no_such_cvar"));
        return nullptr;
    }
    return icvar;
}

}  // namespace

bool GetInt(const char* name, int* out) {
    if (!out) return false;
    void* icvar = ResolveCVar(name, "get_int");
    if (!icvar) return false;
    int val = IcvarSlot<GetIValFn>(icvar, g_getIValSlot)(icvar);
    // The GetIVal target was actually invoked AND returned — record the curated
    // row's VA as an OBSERVED kcdx call (the CALLED-by-kcdx rank-1 signal the
    // startup verification sweep reads). After the call, never before — a record
    // means "invoked + came back", not "pointer resolved".
    survival_verify::RecordKcdxInvocation(g_getIValRowVa);
    *out = val;
    return true;
}

bool GetFloat(const char* name, float* out) {
    if (!out) return false;
    void* icvar = ResolveCVar(name, "get_float");
    if (!icvar) return false;
    float val = IcvarSlot<GetFValFn>(icvar, g_getFValSlot)(icvar);
    // The GetFVal target was actually invoked AND returned — record the curated
    // row's VA (CALLED-by-kcdx rank-1 signal). After the call returns.
    survival_verify::RecordKcdxInvocation(g_getFValRowVa);
    *out = val;
    return true;
}

bool Init() {
    if (g_ready.load(std::memory_order_acquire)) return true;

    // Resolve gEnv->pConsole storage by canonical name (id 10). Same
    // storage console::Init() resolves — we deref it per call, not here.
    uintptr_t pConsole_storage = refdb::ResolveAddrByName("gEnv_pConsole");
    if (!pConsole_storage) {
        log::Warn("[cvar] Init: refdb name \"gEnv_pConsole\" did not resolve "
                  "— kcdx.cvar.* will be unavailable");
        return false;
    }

    // Resolve IConsole::GetCVar by canonical name (id 16). A flat function
    // call (rcx=IConsole*, rdx=name) — the recorded RVA is the call target.
    uintptr_t getCVarVA = refdb::ResolveAddrByName("IConsole_GetCVar");
    if (!getCVarVA) {
        log::Warn("[cvar] Init: refdb name \"IConsole_GetCVar\" did not "
                  "resolve — kcdx.cvar.* will be unavailable");
        return false;
    }

    // Read the ICVar accessor VTABLE SLOT INDICES from the DB. These are
    // vtable methods on the concrete CVar subclass GetCVar returns, NOT
    // flat functions — so we want the slot index (NameResolution.vtable_slot),
    // dispatched through the runtime object's own vtable, not the recorded
    // RVA (which is only one concrete subclass's body). See the g_*Slot
    // declarations' SOURCE comment.
    refdb::NameResolution iv = refdb::ResolveByName("ICVar_GetIVal");
    refdb::NameResolution fv = refdb::ResolveByName("ICVar_GetFVal");
    if (!iv.found || !iv.has_vtable_slot) {
        log::Warn("[cvar] Init: refdb name \"ICVar_GetIVal\" did not resolve "
                  "with a vtable slot — kcdx.cvar.get_int will be unavailable");
        return false;
    }
    if (!fv.found || !fv.has_vtable_slot) {
        log::Warn("[cvar] Init: refdb name \"ICVar_GetFVal\" did not resolve "
                  "with a vtable slot — kcdx.cvar.get_float will be "
                  "unavailable");
        return false;
    }

    g_pConsoleStorage = reinterpret_cast<void*>(pConsole_storage);
    g_GetCVar         = reinterpret_cast<GetCVarFn>(getCVarVA);
    g_getIValSlot     = iv.vtable_slot;
    g_getFValSlot     = fv.vtable_slot;

    // The curated ROW VAs (WhgameBase()+rva) for the invocation record's key —
    // the SAME VA the startup verification sweep resolves these rows to. NOT the
    // runtime call target; the curated row's identity. ResolveAddrByName yields
    // WhgameBase()+rva (0 on a DB that predates the entity → a harmless no-op
    // key). Resolved here once, used to stamp the record after each Get* returns.
    g_getIValRowVa = refdb::ResolveAddrByName("ICVar_GetIVal");
    g_getFValRowVa = refdb::ResolveAddrByName("ICVar_GetFVal");

    g_ready.store(true, std::memory_order_release);

    LOG_INFO_KV("CVAR", "ready",
                log::KV("gEnv_pConsole", reinterpret_cast<void*>(pConsole_storage)),
                log::KV("IConsole_GetCVar", reinterpret_cast<void*>(getCVarVA)),
                log::KV("ICVar_GetIVal_slot", static_cast<long long>(g_getIValSlot)),
                log::KV("ICVar_GetFVal_slot", static_cast<long long>(g_getFValSlot)));
    return true;
}

}  // namespace kcdx::cvar
