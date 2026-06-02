// === Asset overlay — production hook on the game's pak resolver ======
//
// See asset_overlay.h for the full framing. This installs the production
// CCryPak::FOpen overlay hook through the conflict engine (hook_chain::
// AddCEngine), mirroring the engine.lua_pcall engine-direct install site in
// hooks.cpp: resolve by name, build the payload, parse the verified ABI into
// the signature, register the engine-stamped chain entry, then record the
// modification in the live inventory. The body is PASS-THROUGH this step (call
// original unchanged); the overlay-map redirect is a later step.

#include "asset_overlay.h"

#include <atomic>
#include <cstdint>

#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "log.h"
#include "modification_inventory.h"
#include "refdb.h"

namespace kcdx::asset_overlay {

namespace {

// Canonical refdb name for the engine-wide open-by-path resolver. The seed row
// (kcdx_id 131, body at WHGame+0x004614A0) already exists — the common named-
// target path carries address AND verified ABI; no RVA literal, no new seed row.
constexpr const char* kNameFOpen = "CCryPak_FOpen";

// SOURCE: verified seed signature for kcdx_id 131 (CCryPak_FOpen) —
//   ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)
//   RCX=this(ICryPak*), RDX=pName, R8=szMode, R9D=nFlags.
// Win64 __fastcall. The ABI is the Address Library's, not a prologue-shape
// guess. The DSL drives the chain's JIT marshaling; OverlayFOpen below carries
// the matching Before-mode cFn ABI (the chain calls the original itself).
constexpr const char* kFOpenSig =
    "ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)";

std::atomic<bool> g_installed{false};

}  // namespace

// The C function AddCEngine installs (the chain's `cFn`), Before mode. ABI is
// the chain's Before-mode cFn shape — `void cFn(uintptr_t args[], int* outCount,
// <typed args from cSig>)` — NOT the target's own ABI (the chain owns the
// MinHook detour and calls the original itself AFTER the Before callbacks; a
// Before entry never calls the original and returns void). Mirrors
// HookedLuaPcall_Engine in hooks.cpp exactly.
//
// PASS-THROUGH this step: observe nothing, mutate nothing, run the original
// unchanged. The overlay-map lookup + pName rewrite (writing args[1] back
// through the outCount channel) is a later step that fills this body.
//
// INVARIANT: FOpen is hot (the RE found 680 call sites; it fires thousands of
// times). NO per-call log here — that is logging.md + memory.md. The only log
// is the one-shot install line in Install(), never inside this body.
extern "C" void OverlayFOpen(uintptr_t /*args*/[], int* /*outCount*/,
                             void*       /*self*/,
                             const char* /*pName*/,
                             const char* /*szMode*/,
                             uint32_t    /*nFlags*/) {
    // Pass-through: the chain runs the original after this returns.
}

bool Install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    auto sigParse = kcdx::hook_signature::Parse(kFOpenSig);
    if (!sigParse.ok) {
        log::ErrorF("engine.ccrypak_fopen: signature parse failed: %s",
                    sigParse.error.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // COMMON named-target locator: addressName resolves to the body AND the
    // verified ABI (the disassembler test) via the self > engine > other walk.
    kcdx::hook_payload::HookPayload p;
    p.mode         = kcdx::hook_payload::Mode::Before;
    p.addressName  = kNameFOpen;
    p.signature    = sigParse.sig;
    p.hasSignature = true;
    p.owningPlugin = "kcdx";
    p.owningAuthor = "kcdx";
    p.name         = "engine.ccrypak_fopen";

    // priority 0 — mirrors the engine.lua_pcall AddCEngine site (hooks.cpp);
    // priority orders engine entries among THEMSELVES only (engine-vs-plugin is
    // decided by the engine stamp, not priority).
    auto add = kcdx::hook_chain::AddCEngine(
        p, reinterpret_cast<void*>(&OverlayFOpen),
        sigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.ccrypak_fopen",
        /*handleId=*/0);
    if (!add.ok) {
        log::ErrorF("engine.ccrypak_fopen: AddCEngine failed: %s",
                    add.reason.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // Record the engine-owned hook in the live modification inventory
    // (Category::Engine), keyed by the resolved VA — same pattern as the
    // lua_pcall / update engine sites. ResolveAddrByName reads the cache built
    // in refdb::Open() (this runs after RefdbOpened); 0 = miss.
    uintptr_t fopenVA = kcdx::refdb::ResolveAddrByName(kNameFOpen);
    if (fopenVA) {
        kcdx::modification_inventory::RegisterModification(
            fopenVA, kcdx::modification_inventory::Category::Engine,
            "fopen_overlay");
    } else {
        log::Warn("engine.ccrypak_fopen: refdb name \"CCryPak_FOpen\" did not "
                  "resolve for the modification-inventory record (hook still "
                  "installed via the chain by name)");
    }

    log::InfoF("engine.ccrypak_fopen: production FOpen overlay hook installed "
               "(via hook_chain::AddCEngine; pass-through body) at %p",
               reinterpret_cast<void*>(fopenVA));
    return true;
}

}  // namespace kcdx::asset_overlay
