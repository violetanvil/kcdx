// kcdx.declare(...) / kcdx.declared(name) — Lua binders for the
// author-declared track of the unified named-target table.
//
// kcdx.declare is the WRITE surface: an author registers a per-version
// named target under their own plugin's <author>.<plugin>.<bare>
// triple. The declared-targets store (declared_targets.{h,cpp}) is the
// data layer; this binder is the Lua front door. Validation that needs
// store-level knowledge (name charset, version-key shape, pattern-
// without-signature) lives in declared_targets::Register and writes a
// structured KV reject line on its own — this binder propagates the
// boolean result to Lua.
//
// kcdx.declared(name) is the READ surface for declared VALUE entries
// (the ["1.5.1164953"] = 0x0F shape). For PATTERN declarations the
// resolved address is consumed through the hook/bytes/code verbs
// (separate module); kcdx.declared returns nil for them.
//
// Lua precision: declared VALUE integers are pushed via lua_pushinteger.
// LUA_NUMBER=float in the CryEngine build, so integer values >= 2^24
// lose precision on the way back through Lua (lua-precision.md). The
// spec's declared-value examples are small bitmasks (0x0F, 0x1F, slot
// offsets) well under that threshold; pointer-magnitude values resolve
// through the address path (kcdx.hook.<name> / kcdx.bytes.<name> /
// kcdx.code.<name>), not through kcdx.declared. An author who needs to
// surface a pointer-magnitude integer through this accessor should
// declare it as a string and parse it in Lua.

#pragma once

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_declare {

// Register kcdx.declare(...) and kcdx.declared(name) on the kcdx table
// at the top of the Lua stack. Called once from lua_bind.cpp's
// RegisterKcdxTable sequence. Idempotent in shape (overwriting the
// fields is safe), though it is invoked once per process.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_declare
