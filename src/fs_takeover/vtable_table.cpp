#include "vtable_table.h"

#include "open_slots.h"      // kcdx_AdjustFileName / kcdx_FOpenRaw (slot 1 / 35 impls)
#include "read_slots.h"      // the KCDX read-family slot impls (38..66)
#include "metadata_slots.h"  // the KCDX existence/metadata slot impls (13/45/67/68/69/70/92/93)
#include "enum_slots.h"      // the KCDX directory-enumeration slot impl (14)
#include "find_slots.h"      // the KCDX directory-iterator slot impls (63/64/65)

namespace kcdx::fs_takeover {

namespace {

// A THUNK row: forwards the slot to the engine's original body. The role string
// carries the verified slot meaning where one is known (for review + the swap
// log); kcdx_fn is null because construction substitutes the captured original
// pointer for a THUNK row.
constexpr SlotRow Thunk(size_t slot, const char* role) {
    return SlotRow{ slot, role, Impl::Thunk, nullptr };
}

// A KCDX row: kcdx owns the slot — the built vtable entry is the kcdx fn pointer.
// NOT constexpr (a function-pointer-to-void* cast is not a constant expression),
// so a KCDX row is a runtime-initialized SlotRow built at static-init (the table
// is a const array, not a constexpr one).
SlotRow Kcdx(size_t slot, const char* role, void* fn) {
    return SlotRow{ slot, role, Impl::Kcdx, fn };
}

// The per-slot table. KCDX-owned families:
//   - OPEN (1/35/36) + READ (38/39/40/41/43/44/46/47/53/54/55/56/57/58/59/66)
//     — kcdx owns every file open + read on its own CRT (the open+read cutover,
//     design §4.4/§4.5/§5).
//   - EXISTENCE / METADATA-by-name (13/45/67/68/69/70/92/93) — kcdx answers
//     existence/size/attr/stat from the unified index (hit), else thunks the
//     slot's OWN captured original engine body (miss → engine pak-dir AND disk,
//     returns a value, mints no handle, uses no CRT — §-safe; the 8 originals
//     are captured at swap time via SetMetadataOriginals).
//     (metadata_slots.cpp, design §4.5 "Existence / metadata by name".)
//   - DIRECTORY ENUMERATION (14 + 63/64/65) — kcdx ForEachFile (14, the
//     single-call callback API) AND the FindFirst/FindNext/FindClose iterator
//     triplet (63/64/65, the stateful by-name dir walk the table-DB override-glob
//     dispatches through) enumerate the UNIFIED set: the engine's on-disk entries
//     (kcdx CRT find-walk) PLUS the index's pak-resident vpaths under the prefix
//     (the totalizing invariant, design §1/§4.5/§5.1). (enum_slots.cpp /
//     find_slots.cpp.) The triplet mints + owns a kcdx find-handle cradle-to-grave
//     — KI-0027 (err_id=259) is the table-DB glob seeing no pak entries while
//     63/64/65 were THUNK.
// Every other slot is THUNK (the pure-internal plumbing — pool, CRC/MD5,
// %USER%, dir-casing, mount, search-path, delete/copy — kept the engine's for
// now, each a future one-line flip). Slots 15 (ForEachFile's internal callback
// dispatcher) and 101 (the CCryPakFindData iterator factory) stay THUNK as
// surfaced decisions (see the step deliverable) — slot 15 has no independent
// kcdx answer (slot 14 invokes it via the object's vtable+0x78); slot 101's
// multi-object iterator lifecycle is a separable concern. The role strings name
// the verified slot meanings from the vtable-surface analysis. Reversibility
// (the load-bearing property): flipping any Thunk(...) to a Kcdx(...) row is a
// one-line edit here; nothing outside this array reads a slot's ownership.
//
// §4.4 CONSTRAINT: every handle-operating READ slot is KCDX, never THUNK — a
// thunked read slot would fread the kcdx handle-id on the ENGINE's CRT (the
// cross-CRT straddle the takeover removes). The read rows below are the slots
// that constraint binds.
//
// INVARIANT: exactly kCCryPakSlotCount rows, indices 0..kCCryPakSlotCount-1 in
// order; row i describes slot i (construction asserts this).
const SlotRow kTable[] = {
    Thunk(0,   "dtor"),
    Kcdx(1,    "AdjustFileName (resolution chokepoint)",
               reinterpret_cast<void*>(&kcdx_AdjustFileName)),
    Thunk(2,   "config getter"),
    Thunk(3,   "config getter"),
    Thunk(4,   "config getter"),
    Thunk(5,   "config getter"),
    Thunk(6,   "internal"),
    Thunk(7,   "AddPakToValidate"),
    Thunk(8,   "internal"),
    Thunk(9,   "internal"),
    Thunk(10,  "internal"),
    Thunk(11,  "internal"),
    Thunk(12,  "internal"),
    Kcdx(13,   "IsFolder", reinterpret_cast<void*>(&kcdx_IsFolder)),
    Kcdx(14,   "ForEachFile", reinterpret_cast<void*>(&kcdx_ForEachFile)),
    // slot 15 stays THUNK: it is the engine's INTERNAL per-entry callback
    // dispatcher invoked BY slot 14 (builds a path + forwards to the caller's
    // enumeration callback against engine member offsets) — it has no
    // independent kcdx answer. kcdx_ForEachFile invokes it through the object's
    // vtable+0x78 entry, which this row keeps the engine original. (Surfaced
    // decision — see the step deliverable.)
    Thunk(15,  "ForEachFile callback"),
    Thunk(16,  "internal"),
    Thunk(17,  "pak-membership"),
    Thunk(18,  "internal"),
    Thunk(19,  "AddMod"),
    Thunk(20,  "RemoveMod"),
    Thunk(21,  "GetMod"),
    Thunk(22,  "SetAlias"),
    Thunk(23,  "alias-insert"),
    Thunk(24,  "GetAlias"),
    Thunk(25,  "internal"),
    Thunk(26,  "internal"),
    Thunk(27,  "internal"),
    Thunk(28,  "dir-casing/MakeDir"),
    Thunk(29,  "internal"),
    Thunk(30,  "internal"),
    Thunk(31,  "internal"),
    Thunk(32,  "FindPakByCRC"),
    Thunk(33,  "GetPakInfo"),
    Thunk(34,  "GetPakInfo free"),
    Kcdx(35,   "FOpenRaw", reinterpret_cast<void*>(&kcdx_FOpenRaw)),
    // === slot 36 — FOpen. KcdxFOpenMarker fires the cap-108 seating signal on
    // its first fire (the swap is live), then delegates to the real kcdx FOpen
    // impl (open_slots.cpp) on every fire — a real kcdx open that mints a kcdx
    // handle. kcdx_fn is runtime-initialized (the function-pointer-to-void* cast
    // is not a constant expression, so the table is a const, not constexpr, array). ===
    SlotRow{ kSlotFOpen, "FOpen (seating marker → real kcdx open)", Impl::Kcdx,
             reinterpret_cast<void*>(&KcdxFOpenMarker) },
    Thunk(37,  "internal"),
    Kcdx(38,   "FReadRaw-by-pak-index",
               reinterpret_cast<void*>(&kcdx_FReadRaw_byPakIndex)),
    Kcdx(39,   "FReadRaw", reinterpret_cast<void*>(&kcdx_FReadRaw)),
    Kcdx(40,   "FGetCachedFileData",
               reinterpret_cast<void*>(&kcdx_FGetCachedFileData)),
    Kcdx(41,   "FWrite", reinterpret_cast<void*>(&kcdx_FWrite)),
    Thunk(42,  "internal"),
    Kcdx(43,   "FGets", reinterpret_cast<void*>(&kcdx_FGets)),
    Kcdx(44,   "FGetc", reinterpret_cast<void*>(&kcdx_FGetc)),
    Kcdx(45,   "GetFileSize", reinterpret_cast<void*>(&kcdx_GetFileSize)),
    Kcdx(46,   "FGetSize", reinterpret_cast<void*>(&kcdx_FGetSize)),
    Kcdx(47,   "FUngetc", reinterpret_cast<void*>(&kcdx_FUngetc)),
    Thunk(48,  "internal"),
    Thunk(49,  "RemoveFile"),
    Thunk(50,  "RemoveDir"),
    Thunk(51,  "internal"),
    Thunk(52,  "CopyFile"),
    Kcdx(53,   "FSeek", reinterpret_cast<void*>(&kcdx_FSeek)),
    Kcdx(54,   "FTell", reinterpret_cast<void*>(&kcdx_FTell)),
    Kcdx(55,   "FClose", reinterpret_cast<void*>(&kcdx_FClose)),
    Kcdx(56,   "FEof", reinterpret_cast<void*>(&kcdx_FEof)),
    Kcdx(57,   "FError", reinterpret_cast<void*>(&kcdx_FError)),
    Kcdx(58,   "FGetErrno", reinterpret_cast<void*>(&kcdx_FGetErrno)),
    Kcdx(59,   "FFlush", reinterpret_cast<void*>(&kcdx_FFlush)),
    Thunk(60,  "pool memory"),
    Thunk(61,  "pool memory"),
    Thunk(62,  "internal"),
    // slots 63/64/65 — FindFirst/FindNext/FindClose (the stateful directory-
    // iterator triplet, vtable +0x1F8/+0x200/+0x208). KCDX owns the full
    // find-handle lifecycle over the unified set (design §5.1): FindFirst seeds a
    // kcdx find-handle (the engine's on-disk walk UNION the index's pak vpaths
    // under the prefix, loose-skip de-duped), FindNext advances it, FindClose
    // releases it. The engine never operates the iterator — it holds the
    // kcdx-minted handle and passes it back, cradle-to-grave (§4.4). This is the
    // surface the table-DB override-glob (`Libs/Tables/<base>__*.<ext>`) and the
    // general by-name dir listing dispatch through; with these THUNK kcdx served
    // no pak-resident entries and the table-database load fatalled (err_id=259,
    // KI-0027). Slot 101 (the separate CCryPakFindData factory) stays THUNK.
    Kcdx(63,   "FindFirst", reinterpret_cast<void*>(&kcdx_FindFirst)),
    Kcdx(64,   "FindNext",  reinterpret_cast<void*>(&kcdx_FindNext)),
    Kcdx(65,   "FindClose", reinterpret_cast<void*>(&kcdx_FindClose)),
    Kcdx(66,   "FGetModificationTime",
               reinterpret_cast<void*>(&kcdx_FGetModificationTime)),
    Kcdx(67,   "IsFileExist(3)",
               reinterpret_cast<void*>(&kcdx_IsFileExist3)),
    Kcdx(68,   "GetFileAttributes",
               reinterpret_cast<void*>(&kcdx_GetFileAttributes)),
    Kcdx(69,   "GetFileStat", reinterpret_cast<void*>(&kcdx_GetFileStat)),
    Kcdx(70,   "IsFileExist(2)",
               reinterpret_cast<void*>(&kcdx_IsFileExist2)),
    Thunk(71,  "OpenPack/mount"),
    Thunk(72,  "TestArchive"),
    Thunk(73,  "internal"),
    Thunk(74,  "internal"),
    Thunk(75,  "internal"),
    Thunk(76,  "internal"),
    Thunk(77,  "%USER% expansion"),
    Thunk(78,  "internal"),
    Thunk(79,  "internal"),
    Thunk(80,  "internal"),
    Thunk(81,  "CRC/MD5"),
    Thunk(82,  "CRC/MD5"),
    Thunk(83,  "CRC/MD5"),
    Thunk(84,  "internal"),
    Thunk(85,  "internal"),
    Thunk(86,  "internal"),
    Thunk(87,  "internal"),
    Thunk(88,  "internal"),
    Thunk(89,  "internal"),
    Thunk(90,  "internal"),
    Thunk(91,  "GetPakPriority"),
    Kcdx(92,   "GetFileSizeOnDisk",
               reinterpret_cast<void*>(&kcdx_GetFileSizeOnDisk)),
    Kcdx(93,   "GetFileSizeCompressed",
               reinterpret_cast<void*>(&kcdx_GetFileSizeCompressed)),
    Thunk(94,  "RegisterSystemSearchPath"),
    Thunk(95,  "internal"),
    Thunk(96,  "internal"),
    Thunk(97,  "internal"),
    Thunk(98,  "pool memory"),
    Thunk(99,  "pool memory"),
    Thunk(100, "ClosePakByIndex"),
    // slot 101 stays THUNK: the CCryPakFindData factory mints a multi-object-
    // lifetime iterator consumed via a SEPARATE object vftable (FindNext/
    // FindClose) — reimplementing that lifecycle is a separable concern surfaced
    // as a decision, not silently chosen this step. (See the step deliverable.)
    Thunk(101, "FindFirst (CCryPakFindData factory)"),
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == kCCryPakSlotCount,
              "the per-slot table must carry exactly kCCryPakSlotCount rows, "
              "one per CCryPak vtable slot");

}  // namespace

const SlotRow* GetSlotTable(size_t* outCount) {
    if (outCount) *outCount = kCCryPakSlotCount;
    return kTable;
}

}  // namespace kcdx::fs_takeover
