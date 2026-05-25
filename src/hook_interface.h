#pragma once

// kcdx::hook_interface — the engine-side impl of kcdxHookInterface (the C++
// mirror of the Lua kcdx.hook.* surface). Each of the six sub-verb
// installers (Before/After/Around/Replace/Mid/Callsite) builds a
// hook_payload::HookPayload from the caller's (target, callback, opts)
// triple, parses the signature (or pulls it from address_library by
// target name), and queues a Kind::Hook lua_registry::Entry whose payload
// carries cFn so ApplyHookEntry routes through hook_chain::AddC. The four
// query thunks (IsApplied/GetReason/GetName/Uninstall) walk the registry
// by handleId — same shape as Lua's handle:applied()/:reason()/:name()/
// :uninstall(). See include/kcdx/Interfaces.h:1346-1581 for the public
// ABI contract.
//
// Phase 3 sub-1 step 5-main chunks 3+4. Vtable order matches the header
// at :1515-1575 byte-for-byte.

#include "kcdx/Interfaces.h"

namespace kcdx::hook_interface {

// Return the engine-owned static kcdxHookInterface instance. Stable for
// the process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Hook, ...).
const kcdxHookInterface* GetInterface();

}  // namespace kcdx::hook_interface
