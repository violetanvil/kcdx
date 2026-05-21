#pragma once
#include <cstdint>
#include <string>

namespace kcdx::ldr_notify {

// ============================================================================
// LDR DLL notification — apply before_game-zone [[patch]] entries
// against modules at the moment they're mapped, BEFORE that module's
// own DllMain runs.
//
// Lifecycle:
//
//   1. kcdx.asi DllMain (DLL_PROCESS_ATTACH).
//   2. paths::Init() + config::LoadAllConfigs() populate g_patches.
//   3. load_order::Read() + Resolve() compute each plugin's zone.
//   4. ApplyAlreadyLoaded() walks every entry whose plugin sits in
//      before_game, finds entries whose target module is ALREADY
//      mapped in the process (only ntdll + kernel32 at this point
//      under most loaders), and applies them.
//   5. Register() installs an LdrRegisterDllNotification callback.
//      For every subsequent module load (WHGame.dll, dinput8.dll,
//      etc.) we get notified BEFORE that module's DllMain fires.
//      The callback applies any before_game-zoned patches whose
//      module matches the just-mapped DLL.
//
// Loader-safety contract (the callback runs under ntdll's loader lock):
//
//   ALLOWED  — GetModuleHandleW, VirtualProtect, memcpy, the deferred-
//              log buffer (mirrors to OutputDebugStringA + queues for
//              later file flush).
//   FORBIDDEN — MinHook init, LoadLibrary / FreeLibrary, CreateThread,
//               kcdx::log file writes (file isn't open yet anyway),
//               anything that may chain-load a delay-loaded DLL,
//               std::iostream.
//
// Per the loader-safety contract, before_game zone only accepts
// [[patch]] entries. Capability gating in load_order.cpp already
// downgrades hook/mid_hook/trampoline plugins to after_game.
//
// This module is a no-op when KCDX_BEFORE_GAME_ZONE != "1" (the
// env-var feature flag during PR 2 bring-up).
// ============================================================================

// Walk before_game-zone [[patch]] entries; apply any whose target
// module is currently mapped in this process. Used once at DllMain
// to handle modules that loaded before kcdx.asi did.
//
// Returns the number of entries successfully applied (incl. idempotent
// skips counted as "applied"). Logs via kcdx::log — the deferred buffer
// captures these and flushes once log::Init runs.
size_t ApplyAlreadyLoaded();

// Install the LdrRegisterDllNotification callback. The callback fires
// every time the loader maps a fresh DLL into the process. Idempotent;
// second call is a no-op.
//
// Returns true on success, false if LdrRegisterDllNotification couldn't
// be resolved (very old Windows — kcdx targets Win10+ so this should
// always succeed) or returned a non-zero NTSTATUS.
bool Register();

// Unregister. Optional — kcdx doesn't currently call this. Documented
// for completeness; the registration is process-lifetime by default.
void Unregister();

}  // namespace kcdx::ldr_notify
