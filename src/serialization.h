#pragma once
#include <cstdint>

#include "kcdx/Interfaces.h"

namespace kcdx::serialization {

// Single-sourced cosave tag hash: maps a human-readable string tag
// (e.g. "counter") to the uint32_t the on-disk Chunk.tag stores.
//
// This is THE one hash for cosave tags — called by both the C++
// OpenRecordNamed thunk and the Lua cosave binder (step 2), so the
// two surfaces produce the SAME u32 for the SAME string and interop
// cleanly. Never re-implement; call this. FNV-1a 32-bit over the tag
// bytes — wide and well-distributed; collisions are detected and
// taught at write time (see Thunk_OpenRecordNamed).
uint32_t HashTag(const char* tag);

// Get the published kcdxSerializationInterface implementation. Returned
// pointer is owned by the engine and remains valid for the process
// lifetime. Used by interfaces.cpp's QueryInterface dispatch.
const kcdxSerializationInterface* GetInterface();

// Engine-side init. Idempotent.
void Init();

// Engine-internal hook called by messaging::FireEngineMessage for
// every engine-fired message. Routes save/load lifecycle messages
// into the cosave flush/load pipeline. Engine-only; not part of the
// plugin-facing API surface.
void OnEngineMessage(kcdxMessage* msg);

// Engine-internal: save_load_hooks stashes the FULL SaveGame path
// here before normalizing to basename for the plugin-facing
// kcdxMessage_SaveGame. Used by OnSaveGame to compute the cosave
// path. Engine-only.
void SetLastSaveFullPath(const char* fullPath);

// Engine-internal: save_load_hooks stashes the playline index that
// the engine's slot resolver passed alongside the about-to-be-loaded
// SaveGameRecord. Used by OnPostLoadGame to construct the cosave
// path with the CORRECT `playline<N>` directory (rather than
// re-using whatever directory the most-recent SAVE happened in,
// which would be wrong when the user loads a save from a different
// playline than they last saved in). Set per-load; cleared after
// PostLoadGame fires. Engine-only.
void SetPendingLoadPlayline(int32_t playline);

}  // namespace kcdx::serialization
