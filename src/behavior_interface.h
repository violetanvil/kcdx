#pragma once

// kcdx::behavior_interface — the engine-side impl of kcdxBehaviorInterface (the
// C++ mirror of the Lua kcdx.behavior.* surface). The four verbs (Declare / Set /
// Get / List) route into the SAME behavior_registry both languages share; the
// engine-owned value-handle model (coercion + table-traversal accessors,
// generation-checked staleness) and the C++-side value builders pin + read values
// ON the one VM (main thread) — values are NEVER marshalled out. The query
// thread-wall (load-wave-gated + main-thread post-load) and the value builders
// need the live VM; an off-thread post-load query/build fails loud with a teaching
// error. See include/kcdx/Interfaces.h for the public ABI contract.
//
// The handle map (opaque kcdxBehaviorValue -> behavior full name + value-ref kind
// + the generation it was minted against) lives in this TU; a stale handle (the
// behavior's generation has advanced since mint) is a generation-checked teaching
// error, never a dangle into a replaced ref.

#include "kcdx/Interfaces.h"

namespace kcdx::behavior_interface {

// Return the engine-owned static kcdxBehaviorInterface instance. Stable for the
// process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Behavior, ...).
const kcdxBehaviorInterface* GetInterface();

}  // namespace kcdx::behavior_interface
