#pragma once

#include <cstddef>
#include <string>

// Record synthesis — builds from-scratch wh::I_Mod records for the mod-loader
// absorb (docs/mod-loader-absorb.md).
//
// kcdx IS the mod loader: it owns WHICH mods load and in what ORDER. To do
// that with the "rebuild-wholesale" model (settled by verification against the binary),
// kcdx must hand the native MOUNT pass an enabled-list of I_Mod records it
// constructed itself — a kcdx-plugin has no native record to clone. This
// module is the building block: given the author-facing strings, it allocates
// + populates one 0x70-byte I_Mod record whose layout the native MOUNT pass +
// every downstream pass (localization / table-patch / mod.cfg) accept verbatim.
//
// Verification against the binary proved a kcdx-ALLOCATED record with
// harvested vtables survives MOUNT, and that the I_Mod vtable pair is a
// single concrete class, ASLR-stable across boots (seed rows 3105/3106).
// The OPEN this module closes
// is whether kcdx-SYNTHESIZED string buffers (owned here, not copied from a
// native record) also survive — so the strings are owned with the same
// process-lifetime guarantee as the records and stay address-stable across
// later BuildRecord calls (see the .cpp container choice).

namespace kcdx::mod_absorb {

// The author-facing data a synthesized I_Mod record needs. NOT raw bytes — the
// module maps each field onto the verified I_Mod offset
// (docs/mod-loader-absorb.md "The I_Mod record layout").
struct ModRecordInput {
    std::string rootPathSlash;    // mod root dir WITH trailing '/'    -> +0x08
    std::string id;               // mod id / folder name              -> +0x10
    std::string rootPathNoSlash;  // mod root dir WITHOUT trailing '/' -> +0x20
    std::string displayName;      //                                   -> +0x28
    std::string description;      //                                   -> +0x30
    std::string author;           //                                   -> +0x38
    std::string version;          //                                   -> +0x40
    std::string createdDate;      //                                   -> +0x48
};

// Build + own one 0x70-byte I_Mod record from `in`. The returned pointer is
// the I_Mod* to place in the engine's enabled list (the caller does the place
// — step 4, the SELECT detour; this module never touches the live engine).
//
// OWNERSHIP / LIFETIME (the load-bearing concern this step proves): the record
// AND its backing string buffers are owned by this module and live for the
// PROCESS lifetime — they must outlive MOUNT + every downstream pass. They are
// stored in growth-stable, append-only containers so a later BuildRecord call
// never relocates an earlier record's strings out from under a .c_str() already
// written into that record (see the .cpp).
//
// Sets:
//   +0x00 = I_Mod primary vtable   (Address Library id 3105, ImodVtable_primary)
//   +0x18 = I_Mod sub-object vtable (Address Library id 3106, ImodVtable_subobject)
//   +0x08/+0x10/+0x20/+0x28/+0x30/+0x38/+0x40/+0x48 = .c_str() of the owned
//           strings, per the field map above.
//   +0x50..0x6F = zeroed (matches the native record's scalar tail, U.6.3).
//
// FAILS LOUD (logs Error naming the unresolved id + the consequence, returns
// nullptr) when either vtable id does not resolve — a record with a null
// vtable WILL crash MOUNT on the first virtual dispatch. The caller must
// null-check the return (a null record must never reach the enabled list).
void* BuildRecord(const ModRecordInput& in);

// Number of records this module currently owns. For the regression test to
// assert the container's growth behavior; not a hot path.
size_t BuiltRecordCount();

}  // namespace kcdx::mod_absorb
