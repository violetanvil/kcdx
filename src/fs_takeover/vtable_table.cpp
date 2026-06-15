#include "vtable_table.h"

namespace kcdx::fs_takeover {

namespace {

// A THUNK row: this spike forwards the slot to the engine's original body. The
// role string carries the verified slot meaning where one is known (for review
// + the swap log); kcdx_fn is null because construction substitutes the
// captured original pointer for a THUNK row.
constexpr SlotRow Thunk(size_t slot, const char* role) {
    return SlotRow{ slot, role, Impl::Thunk, nullptr };
}

// The per-slot table. ALL 102 slots THUNK except slot 36 (FOpen), the one KCDX
// row for this seating spike. The role strings name the verified slot meanings
// from the vtable-surface analysis (forward-slash family groupings); slots
// whose role this spike does not need carry a generic descriptor. Reversibility
// (the load-bearing property): flipping any Thunk(...) to a Kcdx row + a fn is a
// one-line edit here; nothing outside this array reads a slot's ownership.
//
// INVARIANT: exactly kCCryPakSlotCount rows, indices 0..kCCryPakSlotCount-1 in
// order; row i describes slot i (construction asserts this).
const SlotRow kTable[] = {
    Thunk(0,   "dtor"),
    Thunk(1,   "AdjustFileName (resolution chokepoint)"),
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
    Thunk(35,  "FOpenRaw"),
    // === slot 36 — the ONE KCDX row for this spike (FOpen). marker-then-thunk:
    // proves the engine dispatches into kcdx. kcdx_fn is a runtime-initialized
    // function pointer (the table is a const array built at static-init, not a
    // constexpr — a function-pointer-to-void* cast is not a constant expression). ===
    SlotRow{ kSlotFOpen, "FOpen (seating marker)", Impl::Kcdx,
             reinterpret_cast<void*>(&KcdxFOpenMarker) },
    Thunk(37,  "internal"),
    Thunk(38,  "FOpen-by-pak-index"),
    Thunk(39,  "FReadRaw"),
    Thunk(40,  "FGetCachedFileData"),
    Thunk(41,  "FWrite"),
    Thunk(42,  "internal"),
    Thunk(43,  "FGets"),
    Thunk(44,  "FGetc"),
    Thunk(45,  "GetFileSize"),
    Thunk(46,  "fileno variant"),
    Thunk(47,  "FUngetc"),
    Thunk(48,  "internal"),
    Thunk(49,  "RemoveFile"),
    Thunk(50,  "RemoveDir"),
    Thunk(51,  "internal"),
    Thunk(52,  "CopyFile"),
    Thunk(53,  "FSeek"),
    Thunk(54,  "FTell"),
    Thunk(55,  "FClose"),
    Thunk(56,  "FEof"),
    Thunk(57,  "FError"),
    Thunk(58,  "FGetErrno"),
    Thunk(59,  "FFlush"),
    Thunk(60,  "pool memory"),
    Thunk(61,  "pool memory"),
    Thunk(62,  "internal"),
    Thunk(63,  "internal"),
    Thunk(64,  "internal"),
    Thunk(65,  "internal"),
    Thunk(66,  "fileno variant"),
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
