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

// Block the calling thread until WHGame.dll is mapped into the process,
// OR until timeoutMs elapses. Returns true if WHGame.dll became
// available (or was already loaded at call time), false on timeout.
//
// kcdx.exe launches KingdomCome.exe via CREATE_SUSPENDED and injects
// kcdx.dll BEFORE the game starts running, so WHGame.dll is not yet
// loaded when kcdx.dll's DllMain (and the worker thread it spawns)
// runs. The engine's MinHook installs target WHGame.dll, so they must
// wait. Once the launcher calls ResumeThread, KCD2's startup code
// loads WHGame.dll and the LDR notification fires; this function
// signals an event from inside the callback so the worker thread
// resumes.
//
// Idempotent if WHGame.dll is already loaded — the underlying event
// is set in Register() via a one-time GetModuleHandle check, so the
// race "game loaded WHGame.dll before kcdx finished setup" is also
// covered.
//
// Safe to call from the worker thread. Do NOT call from DllMain or
// from inside the LDR notification callback (will deadlock under
// loader lock).
bool WaitForGameDll(unsigned long timeoutMs = 60'000);

}  // namespace kcdx::ldr_notify
