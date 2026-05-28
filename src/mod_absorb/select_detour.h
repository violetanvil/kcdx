#pragma once

#include <windows.h>

#include <vector>

// Worker-side enabled-list machinery — the BUILD half of the mod-loader
// takeover. The DETOUR half (the live mutation of the engine's C_ModManager
// state) is now ctor_bracket.cpp: kcdx fully replaces ModManager_ctor and
// reads the kcdx-built list directly into the synthesized 0x68 block. The
// SELECT detour was retired when kcdx took full ownership of the ctor.
//
// What survives here:
//   - The kcdx-owned, process-lifetime std::vector<void*> of synthesized
//     I_Mod* pointers, populated by BuildEnabledListOnWorker.
//   - The manual-reset readiness event the ctor bracket waits on.
//   - CreateReadyEvent + BuildEnabledListOnWorker — the worker-thread
//     entrypoints called from dllmain in the same slot they were before.
//
// What went away:
//   - The MinHook detour on ModManager_Select.
//   - HookedSelect (the wholesale-replace callback).
//   - The g_tookOver one-shot latch on the SELECT-time repoint (the
//     bracket's own one-shot replaces it).

namespace kcdx::mod_absorb {

// Create the readiness event the ctor-bracket callback waits on. MUST be
// called once on the worker thread BEFORE InstallCtorBracket — the bracket
// goes live the moment InstallCtorBracket returns, and the game thread can
// reach HookedCtor within milliseconds; the event handle must already
// exist by then so the wait gate observes a non-null handle (rather than
// falling through to a not-yet-built list). Idempotent — a second call
// returns immediately. The event is manual-reset, initially unsignaled;
// BuildEnabledListOnWorker signals it after the build completes. Event
// creation is co-located with the event consumer (this file) so the
// lifetime is owned end-to-end by the same translation unit.
void CreateReadyEvent();

// Build the enabled I_Mod* list on the WORKER thread (eager), populating the
// module-static enabled-list storage + diagnostic entries, then SetEvent on
// the manual-reset readiness event (created earlier by CreateReadyEvent).
// Called once from the worker thread immediately after DiscoverAndLoad
// finishes (so the plugin manifests are populated) and before
// save_load_hooks::Install (so the worker's hot path "install hooks ->
// discover -> build list -> signal" is contiguous). Idempotent — a second
// call returns immediately. The event is manual-reset, so once signaled it
// stays signaled; a ctor-bracket callback that arrives after the signal
// returns from its wait immediately. Error-logs if the readiness event was
// not created (programming error — the worker must call CreateReadyEvent
// first).
//
// Decouples the BUILD (worker thread) from the FIRE (game thread inside the
// ctor bracket). The game-thread observable outcome is unchanged from the
// prior SELECT-detour design: the kcdx enabled list is in place by the time
// MOUNT runs; only the call site that READS it moved (HookedSelect →
// HookedCtor).
void BuildEnabledListOnWorker();

// Accessor for the readiness event handle the ctor bracket waits on.
// Returns the handle CreateReadyEvent built (or null if CreateReadyEvent
// has not yet run, or its CreateEventW failed and was already logged loud).
// The bracket acquires-loads the handle via this accessor on the game's
// main thread; CreateReadyEvent + BuildEnabledListOnWorker run on the
// worker thread, so the accessor crosses the worker→game boundary — the
// underlying storage is std::atomic<HANDLE> with release/acquire ordering.
HANDLE GetReadyEventHandle();

// Accessor for the kcdx-built enabled-list storage. Returns a const ref to
// the vector built by BuildEnabledListOnWorker; readers (the ctor bracket)
// read .data() / .size() and write those into the synthesized C_ModManager
// at +0x30 / +0x38 / +0x40. The vector is module-static (process lifetime)
// so the pointer the bracket writes never dangles for MOUNT or downstream
// passes.
const std::vector<void*>& GetEnabledListData();

}  // namespace kcdx::mod_absorb
