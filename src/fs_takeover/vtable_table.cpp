#include "vtable_table.h"

#include "open_slots.h"  // kcdx_AdjustFileName / kcdx_FOpenRaw (slot 1 / 35 impls)
#include "read_slots.h"  // the KCDX read-family slot impls (38..66)

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

// The per-slot table. The OPEN family (1/35/36) + the READ family (38/39/40/41/
// 43/44/46/47/53/54/55/56/57/58/59/66) are KCDX — kcdx owns every file open +
// read on its own CRT (the open+read cutover, design §4.4/§4.5/§5). Every other
// slot is THUNK (the pure-internal plumbing — pool, CRC/MD5, %USER%, dir-casing,
// existence/metadata-by-name, mount, search-path, delete/copy — kept the
// engine's for now, each a future one-line flip). The role strings name the
// verified slot meanings from the vtable-surface analysis. Reversibility (the
// load-bearing property): flipping any Thunk(...) to a Kcdx(...) row is a
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
    Thunk(13,  "IsFolder"),
    Thunk(14,  "ForEachFile"),
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
    Thunk(45,  "GetFileSize"),
    Kcdx(46,   "fileno", reinterpret_cast<void*>(&kcdx_Fileno)),
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
    Thunk(63,  "internal"),
    Thunk(64,  "internal"),
    Thunk(65,  "internal"),
    Kcdx(66,   "FGetModificationTime",
               reinterpret_cast<void*>(&kcdx_FGetModificationTime)),
    Thunk(67,  "IsFileExist(3)"),
    Thunk(68,  "GetFileAttributes"),
    Thunk(69,  "GetFileStat"),
    Thunk(70,  "IsFileExist(2)"),
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
    Thunk(92,  "GetFileSizeOnDisk"),
    Thunk(93,  "GetFileSizeCompressed"),
    Thunk(94,  "RegisterSystemSearchPath"),
    Thunk(95,  "internal"),
    Thunk(96,  "internal"),
    Thunk(97,  "internal"),
    Thunk(98,  "pool memory"),
    Thunk(99,  "pool memory"),
    Thunk(100, "ClosePakByIndex"),
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
