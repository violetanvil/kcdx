// lua_cosave_serial — a standalone tagged binary codec for Lua values.
//
// Turns a Lua value (number, string, boolean, or an arbitrarily nested
// table of those) into a self-describing byte buffer and back into the
// SAME Lua value. This is the core of the kcdx.cosave feature, built and
// reviewed in isolation: it knows nothing about the engine cosave
// interface. The cosave BINDER (a later step) is the only consumer —
// it calls Serialize from a SaveCallback (then hands `out` to
// WriteRecordData(buf, len)) and Deserialize from a LoadCallback (after
// ReadRecordData has filled a buffer).
//
// What the binder's per-record `version` arg is for is a SEPARATE axis:
// that is the author's own per-tag schema version. The header this codec
// writes (kSerialFormatVersion) versions the WIRE FORMAT itself so the
// serializer can evolve without breaking old cosaves. The two never mix.
//
// Wire format is documented in docs/lua/cosave.md. Keep the two in sync.
//
// Raw Lua C API only: no kcdx-side static-const sentinel is introduced
// (N/A — only normal values are pushed). Numbers are stored at the build's
// true sizeof(lua_Number) and round-trip EXACTLY relative to the live
// value: on this CryEngine build
// lua_Number is float, so a stored number is the exact 4-byte float the
// VM already holds — the codec neither widens-then-narrows nor claims to
// restore precision the VM discarded before Serialize ever saw the value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_cosave_serial {

// Read the Lua value at stack index `valueIdx` and APPEND its tagged byte
// encoding to `out` (prefixed by a one-time wire-format header). The Lua
// stack is left exactly as it was found — Serialize only reads at
// `valueIdx`; the push/pop it does while iterating tables with lua_next is
// fully balanced before return.
//
// Returns true on success. Returns false and sets `err` to an
// author-facing teaching message (naming the offending Lua type in the
// author's terms) when the value, or any nested value/key, is not
// serializable: a function / userdata / lightuserdata / thread value, an
// unsupported table key type (only number and string keys are allowed),
// or a cyclic table reference. On failure `out` may have been partially
// appended to — the binder discards `out` when Serialize returns false, so
// no partial buffer is ever written. A nil value at `valueIdx` is rejected
// (a top-level nil has nothing meaningful to persist); a nil VALUE inside a
// table is simply an absent key and is skipped naturally by lua_next.
bool Serialize(lua_State* L, int valueIdx, std::vector<uint8_t>& out,
               std::string& err);

// Parse `len` bytes at `buf` (UNTRUSTED input — a cosave file may be
// truncated, corrupt, or from a newer kcdx) and PUSH the reconstructed Lua
// value onto L's stack. On success exactly ONE value is pushed (the same
// Lua type that was serialized) and the function returns true.
//
// On failure the function returns false, sets `err`, and pushes NOTHING
// (the stack is left exactly as it was found — the caller does not pop on
// the false path). Every read is bounds-checked against `len`; a truncated
// or garbage buffer, an unknown wire-format version, an unknown type tag,
// or a nested-depth overflow yields false + err, never a buffer over-read
// or a crash.
bool Deserialize(lua_State* L, const uint8_t* buf, size_t len,
                 std::string& err);

}  // namespace kcdx::lua_cosave_serial
