#pragma once

// The seating hook — installs the vtable swap at the CCryPak construction site.
//
// kcdx owns the engine filesystem by swapping the CCryPak object's vtable
// pointer (vtable_swap). The swap must land in a precise window: AFTER the
// engine constructs + publishes the CCryPak pointer into the global env slot,
// and BEFORE the engine's first file call through it. The construction site
// (the helper CSystem::Init calls to build + publish CCryPak) is that window's
// edge — seating there means kcdx owns the object the instant it is published,
// before any consumer dispatches through it.
//
// This is an AFTER-hook (a trampoline-style detour that lets the original run):
// the hook captures the original helper, the callback calls it (so the helper
// constructs + publishes CCryPak exactly as vanilla), then reads the published
// pointer and performs the swap on return. It does NOT replace the helper.
//
// Distinct from the ModManager construction bracket (a separate, later site):
// the bracket FULLY replaces the ModManager ctor and is the mod-loader concern;
// this hook lets the CCryPak helper run and only swaps the vtable afterward, and
// is the filesystem-takeover concern. Two different sites, two different hooks.

namespace kcdx::fs_takeover {

// Install the MinHook detour on the CCryPak construct-store helper (resolved by
// refdb curated name). Idempotent — a second call returns the cached result.
// Logs install failure LOUD and returns false; the rest of boot continues (an
// inactive seating hook means the engine keeps its own CCryPak vtable — no kcdx
// filesystem ownership this boot, but boot proceeds vanilla).
//
// Must be installed EARLY — before CSystem::Init reaches the construct-store
// helper on the game's main thread (the same race window the ModManager bracket
// arms ahead of). INSTALL only registers the hook with MinHook; the swap FIRES
// later, inside CSystem::Init, when the helper runs.
//
// Returns true on a successful install (or an install already succeeded this
// session); false on any failure (refdb miss, MinHook init/create/enable
// failure).
bool InstallSeatingHook();

}  // namespace kcdx::fs_takeover
