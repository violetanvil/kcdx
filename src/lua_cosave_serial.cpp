// lua_cosave_serial — implementation. See lua_cosave_serial.h for the
// contract and docs/lua/cosave.md for the byte-level wire format.

#include "lua_cosave_serial.h"

#include <cstring>

namespace kcdx::lua_cosave_serial {
namespace {

// ----------------------------------------------------------------------------
// Wire-format constants. Mirror these in docs/lua/cosave.md when they change.
// ----------------------------------------------------------------------------

// Buffer magic — two bytes 'K','S' (Kcdx Serial). Distinguishes a kcdx
// codec buffer from arbitrary record bytes and gives Deserialize a cheap
// first sanity check.
constexpr uint8_t kMagic0 = 'K';
constexpr uint8_t kMagic1 = 'S';

// Wire-format version of THIS codec — independent of the author's per-tag
// `version` arg to kcdx.cosave.write. Bump when the byte layout changes;
// Deserialize refuses an unknown version rather than mis-parsing.
constexpr uint8_t kSerialFormatVersion = 1;

// Type tags — one byte preceding each value's payload. Distinguish the
// exact Lua type on read so Deserialize reconstructs number-as-number,
// string-as-string, etc.
constexpr uint8_t kTagNumber = 0x01;  // + sizeof(lua_Number) raw bytes
constexpr uint8_t kTagString = 0x02;  // + [u32 length][length bytes]
constexpr uint8_t kTagBool   = 0x03;  // + 1 byte (0 = false, 1 = true)
constexpr uint8_t kTagTable  = 0x04;  // + [u32 entry-count] + entries

// Header is [magic0][magic1][format-version][lua_Number width]. The width
// byte records sizeof(lua_Number) on the build that WROTE the buffer (4 on
// this float CryEngine build). Deserialize refuses a buffer whose recorded
// width != this build's sizeof(lua_Number): the raw number bytes would be
// reinterpreted under the wrong float format and silently corrupt. This is
// belt-and-suspenders alongside the version byte.
constexpr size_t kHeaderLen = 4;

// Hard recursion depth cap — defense-in-depth against a pathological (but
// acyclic) deeply-nested table on the write side, and against a crafted
// buffer claiming unbounded nesting on the read side. Cycle detection
// (ancestry set) is the primary guard on writes; this bounds stack growth
// regardless.
constexpr unsigned kMaxDepth = 256;

// ---- little-endian u32 helpers (KCD2 is x86-64; bytes stored LE) ----

void AppendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// A bounds-checked cursor over the untrusted read buffer. Every read goes
// through Take(); it never advances past `len`, so a truncated/corrupt
// buffer yields a clean false rather than an over-read.
struct Reader {
    const uint8_t* buf;
    size_t len;
    size_t pos = 0;

    // Returns a pointer to `n` bytes at the cursor and advances, or nullptr
    // if fewer than `n` bytes remain. Guards against pos + n overflow.
    const uint8_t* Take(size_t n) {
        if (n > len || pos > len - n) return nullptr;
        const uint8_t* p = buf + pos;
        pos += n;
        return p;
    }

    bool TakeU32LE(uint32_t& out) {
        const uint8_t* p = Take(4);
        if (!p) return false;
        out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
              (static_cast<uint32_t>(p[2]) << 16) |
              (static_cast<uint32_t>(p[3]) << 24);
        return true;
    }
};

constexpr const char* kTruncatedMsg =
    "cosave record is truncated or corrupt";

// ----------------------------------------------------------------------------
// Serialize — recursive writer. `ancestry` holds the lua_topointer of every
// table currently open on the recursion path; a repeat is a cycle (or a
// shared sub-table — we reject either; shared/cyclic references are not
// preserved, documented). Stack discipline: this reads the value at
// `idx` and, for tables, pushes/pops during lua_next iteration, leaving the
// stack exactly as it found it on return.
// ----------------------------------------------------------------------------

bool SerializeValue(lua_State* L, int idx, std::vector<uint8_t>& out,
                    std::vector<const void*>& ancestry, unsigned depth,
                    std::string& err);

bool SerializeKey(lua_State* L, int idx, std::vector<uint8_t>& out,
                  std::string& err) {
    // Keys are restricted to number and string (the common case: array
    // indices and named fields). A boolean/table/other key is rejected
    // with a teaching error rather than silently dropped.
    int t = lua_type(L, idx);
    if (t == LUA_TNUMBER) {
        out.push_back(kTagNumber);
        lua_Number n = lua_tonumber(L, idx);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&n);
        out.insert(out.end(), p, p + sizeof(lua_Number));
        return true;
    }
    if (t == LUA_TSTRING) {
        size_t slen = 0;
        const char* s = lua_tolstring(L, idx, &slen);  // byte string + length
        if (slen > 0xFFFFFFFFu) {
            err = "cosave string key is too long to serialize (over 4 GB)";
            return false;
        }
        out.push_back(kTagString);
        AppendU32LE(out, static_cast<uint32_t>(slen));
        if (slen) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
            out.insert(out.end(), p, p + slen);
        }
        return true;
    }
    err = std::string("table key of type '") + lua_typename(L, t) +
          "' is not supported — cosave table keys must be numbers or strings";
    return false;
}

bool SerializeValue(lua_State* L, int idx, std::vector<uint8_t>& out,
                    std::vector<const void*>& ancestry, unsigned depth,
                    std::string& err) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER: {
            out.push_back(kTagNumber);
            // Store the FULL lua_Number bytes as the VM holds them. On this
            // build lua_Number is float (4 bytes); we store sizeof(lua_Number)
            // — no widen-to-double-then-narrow, no assumed 8 bytes. Round-trip
            // is exact relative to the live value.
            lua_Number n = lua_tonumber(L, idx);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&n);
            out.insert(out.end(), p, p + sizeof(lua_Number));
            return true;
        }
        case LUA_TBOOLEAN: {
            out.push_back(kTagBool);
            out.push_back(lua_toboolean(L, idx) ? 1 : 0);
            return true;
        }
        case LUA_TSTRING: {
            size_t slen = 0;
            // Lua strings are byte strings (may embed NULs) — use the length,
            // never strlen.
            const char* s = lua_tolstring(L, idx, &slen);
            if (slen > 0xFFFFFFFFu) {
                err = "cosave string is too long to serialize (over 4 GB)";
                return false;
            }
            out.push_back(kTagString);
            AppendU32LE(out, static_cast<uint32_t>(slen));
            if (slen) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
                out.insert(out.end(), p, p + slen);
            }
            return true;
        }
        case LUA_TTABLE: {
            if (depth >= kMaxDepth) {
                err = "cosave table nesting is too deep to serialize";
                return false;
            }
            // Cycle / shared-reference detection: reject if this table is
            // already open on the current path.
            const void* self = lua_topointer(L, idx);
            for (const void* seen : ancestry) {
                if (seen == self) {
                    err =
                        "cyclic table reference — cosave cannot serialize a "
                        "table that references itself (shared or cyclic table "
                        "references are not preserved)";
                    return false;
                }
            }

            out.push_back(kTagTable);
            // Reserve the entry-count slot; backpatch after counting.
            size_t countPos = out.size();
            AppendU32LE(out, 0);
            uint32_t count = 0;

            ancestry.push_back(self);

            // Normalize `idx` to an absolute index — we push during lua_next,
            // so a relative idx would shift under us.
            int tableIdx = idx < 0 ? lua_gettop(L) + idx + 1 : idx;

            lua_pushnil(L);  // first key
            while (lua_next(L, tableIdx) != 0) {
                // stack now: ... key(-2) value(-1)
                // Serialize key first (key at -2), then value (at -1).
                if (!SerializeKey(L, -2, out, err)) {
                    lua_pop(L, 2);  // pop value + key — restore balance
                    ancestry.pop_back();
                    return false;
                }
                if (!SerializeValue(L, -1, out, ancestry, depth + 1, err)) {
                    lua_pop(L, 2);
                    ancestry.pop_back();
                    return false;
                }
                if (count == 0xFFFFFFFFu) {
                    err = "cosave table has too many entries to serialize";
                    lua_pop(L, 2);
                    ancestry.pop_back();
                    return false;
                }
                ++count;
                lua_pop(L, 1);  // pop value, keep key for next lua_next
            }
            // lua_next has popped the final key; stack is back to entry state.

            ancestry.pop_back();

            // Backpatch the entry count (LE).
            out[countPos + 0] = static_cast<uint8_t>(count & 0xFF);
            out[countPos + 1] = static_cast<uint8_t>((count >> 8) & 0xFF);
            out[countPos + 2] = static_cast<uint8_t>((count >> 16) & 0xFF);
            out[countPos + 3] = static_cast<uint8_t>((count >> 24) & 0xFF);
            return true;
        }
        case LUA_TNIL:
            err =
                "cannot serialize a nil value to a cosave (a top-level nil has "
                "nothing to persist; a nil field inside a table is just an "
                "absent key)";
            return false;
        case LUA_TFUNCTION:
            err = "cannot serialize a function to a cosave";
            return false;
        case LUA_TUSERDATA:
            err = "cannot serialize userdata to a cosave";
            return false;
        case LUA_TLIGHTUSERDATA:
            err = "cannot serialize lightuserdata to a cosave";
            return false;
        case LUA_TTHREAD:
            err = "cannot serialize a coroutine/thread to a cosave";
            return false;
        default:
            err = std::string("cannot serialize a value of type '") +
                  lua_typename(L, t) + "' to a cosave";
            return false;
    }
}

// ----------------------------------------------------------------------------
// Deserialize — recursive reader over the bounds-checked cursor. Pushes one
// value per call on success; pushes nothing and returns false on any read
// failure. `depth` mirrors the write-side cap.
// ----------------------------------------------------------------------------

bool DeserializeValue(lua_State* L, Reader& r, unsigned depth,
                      std::string& err) {
    if (depth >= kMaxDepth) {
        err = kTruncatedMsg;  // a buffer claiming this depth is malformed
        return false;
    }
    const uint8_t* tagp = r.Take(1);
    if (!tagp) {
        err = kTruncatedMsg;
        return false;
    }
    uint8_t tag = *tagp;
    switch (tag) {
        case kTagNumber: {
            const uint8_t* p = r.Take(sizeof(lua_Number));
            if (!p) {
                err = kTruncatedMsg;
                return false;
            }
            lua_Number n;
            std::memcpy(&n, p, sizeof(lua_Number));
            lua_pushnumber(L, n);  // exact: same bytes the VM wrote
            return true;
        }
        case kTagBool: {
            const uint8_t* p = r.Take(1);
            if (!p) {
                err = kTruncatedMsg;
                return false;
            }
            lua_pushboolean(L, *p ? 1 : 0);
            return true;
        }
        case kTagString: {
            uint32_t slen = 0;
            if (!r.TakeU32LE(slen)) {
                err = kTruncatedMsg;
                return false;
            }
            const uint8_t* p = r.Take(slen);  // bounds-checked against len
            if (!p && slen != 0) {
                err = kTruncatedMsg;
                return false;
            }
            // slen == 0 -> p may be a valid pointer to zero bytes; pushlstring
            // with len 0 is fine.
            lua_pushlstring(L, reinterpret_cast<const char*>(p), slen);
            return true;
        }
        case kTagTable: {
            uint32_t entries = 0;
            if (!r.TakeU32LE(entries)) {
                err = kTruncatedMsg;
                return false;
            }
            lua_newtable(L);
            for (uint32_t i = 0; i < entries; ++i) {
                // key, then value — both nested values; recurse.
                if (!DeserializeValue(L, r, depth + 1, err)) {
                    lua_pop(L, 1);  // pop the half-built table
                    return false;
                }
                if (!DeserializeValue(L, r, depth + 1, err)) {
                    lua_pop(L, 2);  // pop the key we just pushed + the table
                    return false;
                }
                // stack: ... table(-3) key(-2) value(-1)
                lua_rawset(L, -3);  // table[key] = value; pops key + value
            }
            return true;
        }
        default:
            err = kTruncatedMsg;  // unknown tag — corrupt or newer format
            return false;
    }
}

}  // namespace

bool Serialize(lua_State* L, int valueIdx, std::vector<uint8_t>& out,
               std::string& err) {
    // Normalize to an absolute index up front: SerializeValue's table path
    // pushes onto the stack, which would shift a relative valueIdx.
    int absIdx = valueIdx < 0 ? lua_gettop(L) + valueIdx + 1 : valueIdx;

    // Header: magic, format version, lua_Number width on THIS build.
    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(kSerialFormatVersion);
    out.push_back(static_cast<uint8_t>(sizeof(lua_Number)));

    std::vector<const void*> ancestry;
    int top = lua_gettop(L);
    bool ok = SerializeValue(L, absIdx, out, ancestry, 0, err);
    // SerializeValue is internally balanced; assert no net stack change.
    // (No lua_assert dependency — restore defensively if a path drifted.)
    if (lua_gettop(L) != top) lua_settop(L, top);
    return ok;
}

bool Deserialize(lua_State* L, const uint8_t* buf, size_t len,
                 std::string& err) {
    Reader r{buf, len};

    // Validate the header before touching the payload.
    const uint8_t* h = r.Take(kHeaderLen);
    if (!h) {
        err = kTruncatedMsg;
        return false;
    }
    if (h[0] != kMagic0 || h[1] != kMagic1) {
        err = "cosave record is not a kcdx-serialized value (bad magic)";
        return false;
    }
    if (h[2] != kSerialFormatVersion) {
        err =
            "cosave record was written by a different kcdx serializer version "
            "and cannot be read by this build";
        return false;
    }
    if (h[3] != static_cast<uint8_t>(sizeof(lua_Number))) {
        // A cosave written on a build with a different lua_Number width would
        // mis-read every number — refuse rather than corrupt.
        err =
            "cosave record uses a different numeric format than this build and "
            "cannot be read";
        return false;
    }

    int top = lua_gettop(L);
    if (!DeserializeValue(L, r, 0, err)) {
        // Push nothing on failure — restore the stack to its entry state in
        // case a nested table left a partial value behind.
        if (lua_gettop(L) != top) lua_settop(L, top);
        return false;
    }
    // Exactly one value must have been pushed and no trailing garbage left.
    if (r.pos != r.len) {
        err = "cosave record has trailing bytes after the value (corrupt)";
        if (lua_gettop(L) != top) lua_settop(L, top);
        return false;
    }
    return true;
}

}  // namespace kcdx::lua_cosave_serial
