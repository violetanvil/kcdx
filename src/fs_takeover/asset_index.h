#pragma once

// kcdx's unified asset index — ONE vpath -> ByteSource map, built at load.
//
// The single in-memory directory every kcdx file open consults (file-system-
// takeover design §5). kcdx's resolution slots (slot-1 AdjustFileName, the open/
// existence slots — wired in a later step) resolve a virtual path with ONE O(1) hash lookup
// against this map: no per-call search-path-vector walk, no per-call pak-
// directory bisection, no per-mode existence gate (the "no extra hotpath checks"
// constraint, §5).
//
// Each vpath maps to exactly one ByteSource — where the resolved bytes live:
//   - Loose: a disk file kcdx opens with its own CRT (_wfopen) — a mod override.
//   - Pak:   an entry (offset+size+method+crc) into a pak file kcdx reads with
//            its own PKZIP/DEFLATE reader (pak_reader.{h,cpp}, §6).
//
// Precedence is decided ONCE at build (§5/§7): overlay (loose) WINS vanilla.
// The build composes two already-resolved layers — it does NOT re-implement the
// asset-replacement §4.4 load-order winner / §5.3 cross-mod resolution. Those
// are computed inside the asset-overlay map (asset_overlay.{h,cpp}); the index
// ingests that map's entries as Loose ByteSources and OVERWRITES the pak-derived
// entries at the same vpath. The only precedence the index itself applies is
// loose-over-pak.

#include <cstdint>
#include <string>
#include <unordered_map>

namespace kcdx::fs_takeover {

// Where a resolved vpath's bytes live. A tagged union of the two byte-source
// kinds; field names + semantics mirror design §5
// (ByteSource{ kind, loose{diskPath} | pak{pakFile, offset, size, method, crc} }).
struct ByteSource {
    enum class Kind { Loose, Pak };
    Kind kind = Kind::Pak;

    // --- Loose (kind == Loose): a disk file kcdx opens with its own CRT. -----
    // Absolute path of the loose override file (the overlay map's diskPath).
    std::string diskPath;

    // --- Pak (kind == Pak): an entry into a pak file kcdx reads itself. ------
    // The pak file holding the bytes (kcdx _wfopen's this; the read path is
    // ReadPakEntry, pak_reader.h). wstring — the same form ReadPakEntry takes.
    std::wstring pakFile;
    // Offset of the entry's LOCAL FILE HEADER from the pak's start
    // (PakEntry::local_header_offset). ReadPakEntry seeks here.
    uint64_t offset = 0;
    // Uncompressed size of the entry (PakEntry::uncompressed_size). The size the
    // read family reports + the inflate target buffer size.
    uint64_t size = 0;
    // STORED/COMPRESSED size of the entry (PakEntry::compressed_size) — how many
    // bytes ReadPakEntry reads from the data start before inflating. REQUIRED to
    // read a DEFLATE entry (it differs from `size`); a STORED entry has
    // compressed == uncompressed. The open+read cutover (step 3.2) reads this to
    // serve a vanilla pak entry — without it a DEFLATE read would read the wrong
    // byte count. (§5 lists the ByteSource pak fields illustratively; §6's
    // "kcdx reads the pak entry itself" REQUIRES the compressed extent — this
    // field completes the §6 invariant, it does not add scope.)
    uint64_t compressed = 0;
    // PKZIP compression method: 0 = STORED, 8 = DEFLATE (PakEntry::method).
    uint16_t method = 0;
    // CRC-32 of the uncompressed bytes (PakEntry::crc32).
    uint32_t crc = 0;
};

// The unified index: normalized vpath -> winning ByteSource. The key is the
// SAME normalization the overlay map uses (asset_overlay::NormalizeVPath —
// lowercase + backslash->forwardslash), so loose and pak keys collide correctly
// for the overlay-wins precedence.
using AssetIndex = std::unordered_map<std::string, ByteSource>;

// Build the unified index over gameDataDir (cold path, at load — §5).
//
// Build order (precedence by construction — §5/§7):
//   1. Discover + CDR-parse every `<gameDataDir>/*.pak` (the vanilla pak set;
//      the §5 `assumes` vanilla-pak discovery, resolved as a checkable
//      directory enumeration — std::filesystem). For each entry insert a Pak
//      ByteSource keyed by NormalizeVPath(entry.name). When two paks carry the
//      same vpath, LAST-pak-wins by directory-iteration order (a later pak
//      overwrites an earlier one) — see asset_index.cpp for the rule + its
//      §-cite + the surfaced question (§5/§7 do not pin vanilla-vs-vanilla
//      precedence). One pak that fails to parse is logged + skipped, never
//      aborting the whole index (logging.md, input-validation.md).
//   2. Ingest asset_overlay::GetOverlayMap(): for each OverlayEntry insert/
//      OVERWRITE a Loose ByteSource at its (already-normalized) key. LOOSE WINS
//      VANILLA — the only precedence the index itself applies. The loose-vs-
//      loose + cross-mod precedence was ALREADY decided in the overlay map
//      (§4.4/§5.3); the index does not re-do it.
//
// Emits one LOG_DEBUG_KV summary (entry/pak/loose counts) so the build is
// observable in kcdx-dev.log. Pass gameDataDir as a parameter (testable: a test
// can point it at a fixture set). Returns the built index by value.
//
// NOTE — this does NOT wire the index into boot/init or consult it from any
// slot; that is a later step. This step builds the index + its builder + the lookup.
AssetIndex BuildAssetIndex(const std::wstring& gameDataDir);

// Resolve a vpath to its winning ByteSource, or nullptr on a miss.
//
// The O(1) hot-path lookup (§5): NormalizeVPath the input, one hash lookup into
// `index`, return the ByteSource* or nullptr. No search, no bisection, no per-
// mode gate. A miss means kcdx does not serve this vpath from the index — the
// engine resolves it (the fall-through a later step wires); here, return nullptr.
const ByteSource* ResolveVPath(const AssetIndex& index, const std::string& vpath);

// === The process-lifetime built index — set once at the seat, read forever ===
//
// The seat (seating_hook.cpp, game's main thread, inside CSystem::Init) builds
// the index ONCE after the overlay-ready gate resolves and stores it here; the
// resolution/open slots (a later sub-step) read it on every file open for the
// life of the process. The store mirrors g_kcdxVtable's ownership model
// (vtable_swap.cpp): a process-lifetime static, filled once, never freed — the
// index is read on every file call forever, so the storage can never be
// reclaimed. Producer-only this step: SetBuiltIndex is wired into the seat;
// GetBuiltIndex exists for the slot impls the NEXT sub-step adds (nothing reads
// it yet).

// Move the seat-built index into the process-lifetime store. Set-once: the FIRST
// call takes ownership; a second call is a no-op (the seat latches the build, so
// this fires exactly once, but the guard keeps it honest). The seat calls this
// after the overlay-ready gate resolves and BuildAssetIndex returns.
void SetBuiltIndex(AssetIndex&& index);

// Read the process-lifetime built index. Returns the stored index; before
// SetBuiltIndex has run it is an empty index (a lookup misses — no crash). The
// reference is stable for the process lifetime (the store is never freed).
// Read by the slot impls (next sub-step) on every file open.
const AssetIndex& GetBuiltIndex();

}  // namespace kcdx::fs_takeover
