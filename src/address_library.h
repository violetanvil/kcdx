#pragma once

#include <cstdint>

// Address Library — id-to-RVA lookup compiled into kcdx.asi.
//
// Plugins call api->ResolveAddress(id) to get a runtime VA for a
// known function/data entry without hardcoding RVAs (which would
// break on every KCD2 patch). The Address Library decouples the
// stable plugin-facing IDs from the per-build RVAs.
//
// Seed data + ID assignment policy + per-row provenance:
// _research/phase7-recon/.

namespace kcdx::address_library {

// Resolve a known address-library ID against the running KCD2 build.
// Returns the absolute VA (WHGame.dll base + RVA) on success, or 0
// when:
//   - id is unknown to this kcdx build's compiled-in database; OR
//   - the row for `id` exists but its game_version doesn't match
//     the running KCD2 (plugin needs an updated kcdx with a fresh
//     RVA for this game build); OR
//   - the row's status is anything other than "verified" (we don't
//     promise resolution for unverified rows even when the RVA is
//     present).
//
// Called from interfaces.cpp's Thunk_ResolveAddress.
uintptr_t Resolve(uint64_t id);

// Diagnostic: count of compiled-in entries (used by self-test).
size_t EntryCount();

// Diagnostic: count of entries whose game_version matches the
// running build (i.e. entries that would resolve if the right id
// were queried). Used by the engine's self-test and dev-log
// startup summary.
size_t EntryCountForRunningVersion();

// Resolve a known address by NAME instead of numeric id. Names are
// the kebab/snake-case labels in the seed CSV (e.g. "lua_pcall",
// "cscriptsystem_init"). Returns the same VA as Resolve(id) for the
// matching row, or 0 with the same rules (unknown name, wrong
// game_version, unverified).
//
// Used by kcdx.hook's locator path so authors can write
//   kcdx.hook(kcdx.addr.lua_pcall, { ... })
// rather than remembering numeric ids. Names are advisory (no
// stability guarantee across kcdx versions); numeric ids remain
// the canonical reference for code that needs stability.
uintptr_t ResolveByName(const char* name);

// Iterate every entry that matches the running KCD2 build AND has
// status "verified" — i.e. every row that would resolve via either
// Resolve(id) or ResolveByName(name). Calls `cb` with the entry's
// id, name, and resolved VA for each match. Used to eagerly populate
// kcdx.addr.* at startup.
//
// Stops iterating when `cb` returns false.
using ForEachResolvableCallback = bool (*)(uint64_t id, const char* name,
                                           uintptr_t va, void* userdata);
void ForEachResolvable(ForEachResolvableCallback cb, void* userdata);

}  // namespace kcdx::address_library
