#include "asset_index.h"

#include <cwchar>      // _wcsicmp — case-insensitive .pak extension match
#include <filesystem>  // vanilla-pak discovery: <gameDataDir>/*.pak
#include <string>
#include <vector>

#include "pak_reader.h"
#include "../asset_overlay.h"
#include "../log.h"

// Unified asset index — see asset_index.h for the shape + the build contract.
// file-system-takeover design §5 (the index + the O(1)-lookup property) + §7
// (the precedence is the asset-replacement §4.4/§5.3 precedence, computed once).
//
// asset_index uses only PakEntry + ParsePakCentralDirectory + the overlay map's
// NormalizeVPath/GetOverlayMap — no miniz API directly — so it needs no miniz
// include and no `#undef crc32` (pak_reader.h does not pull miniz; only
// pak_reader.cpp does). PakEntry::crc32 is read here through pak_reader.h alone,
// uncluttered by miniz's zlib-compat crc32 alias macro.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "FS_INDEX";

}  // namespace

AssetIndex BuildAssetIndex(const std::wstring& gameDataDir) {
    namespace fs = std::filesystem;
    AssetIndex index;

    // ---- 1. Vanilla paks: discover + CDR-parse, keyed by normalized vpath. --
    //
    // Discover the vanilla pak set by enumerating <gameDataDir>/*.pak (the §5
    // `assumes` vanilla-pak discovery, resolved as a checkable directory
    // enumeration — not a runtime-mechanism probe). Same std::filesystem
    // iteration pattern mod_absorb/enabled_list_builder.cpp uses for mod paks.
    size_t pakCount = 0;
    size_t pakEntryInserts = 0;

    std::error_code ec;
    fs::directory_iterator it(gameDataDir, ec);
    if (ec) {
        // The Data dir does not resolve (wrong path / absent on this machine).
        // Log + continue: the index is still well-formed (empty of vanilla
        // sources); a caller pointing at a real dir gets the full set. Never a
        // crash, never a silent empty list passed off as whole.
        LOG_ERROR_KV(kCat, "data_dir_open_failed",
                     kcdx::log::KV("data_dir",
                         fs::path(gameDataDir).string()),
                     kcdx::log::KV("error", ec.message()),
                     kcdx::log::KV::BareStr("detail",
                         "vanilla Data dir did not open — index built with no "
                         "vanilla pak sources (overlay sources still ingested)"));
    } else {
        for (const fs::directory_entry& entry : it) {
            std::error_code fec;
            if (!entry.is_regular_file(fec) || fec) continue;
            const std::wstring ext = entry.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".pak") != 0) continue;

            const std::wstring pakPath = entry.path().wstring();
            std::vector<PakEntry> entries;
            std::string parseErr;
            if (!ParsePakCentralDirectory(pakPath, entries, parseErr)) {
                // One bad pak does not abort the whole index — log + skip it,
                // continue with the rest (logging.md every-failure-logged;
                // input-validation.md — a malformed pak is a contract
                // violation to skip, not to trust or crash on).
                LOG_ERROR_KV(kCat, "pak_parse_skipped",
                             kcdx::log::KV("pak", entry.path().string()),
                             kcdx::log::KV("error", parseErr),
                             kcdx::log::KV::BareStr("detail",
                                 "pak central-directory parse failed — this pak "
                                 "skipped, index build continues"));
                continue;
            }
            ++pakCount;

            for (const PakEntry& pe : entries) {
                ByteSource src;
                src.kind    = ByteSource::Kind::Pak;
                src.pakFile = pakPath;
                src.offset  = pe.local_header_offset;
                src.size    = pe.uncompressed_size;
                src.method  = pe.method;
                src.crc     = pe.crc32;
                // LAST-pak-wins on a vanilla-vs-vanilla collision: operator[]
                // INSERTS-OR-OVERWRITES, so a later pak in iteration order
                // replaces an earlier one at the same vpath. This is a
                // deterministic FALLBACK for a case the vanilla set does not
                // exhibit: a static scan of the vanilla Data paks found ZERO
                // cross-pak vpath collisions (each vanilla pak owns a disjoint
                // vpath set — split by content). §5/§7 do not pin vanilla-vs-
                // vanilla precedence precisely because it does not arise; the
                // rule is here so a future game patch that introduced one would
                // resolve predictably (last-mounted wins, mirroring CryEngine's
                // later-pak-overrides), not silently nondeterministically.
                index[asset_overlay::NormalizeVPath(pe.name)] = std::move(src);
                ++pakEntryInserts;
            }
        }
    }

    // ---- 2. Overlay (loose) sources: ingest + OVERWRITE pak — loose wins. ---
    //
    // The overlay map ALREADY computed the asset-replacement §4.4 load-order
    // winner + §5.3 cross-mod resolution, keyed by NormalizeVPath. The index
    // COMPOSES it: each OverlayEntry becomes a Loose ByteSource at its
    // already-normalized key, OVERWRITING any pak source there (loose wins
    // vanilla — §5/§7, the one precedence the index itself applies). The
    // loose-vs-loose precedence was decided in the overlay map; not re-done.
    size_t looseInserts = 0;
    const asset_overlay::OverlayMap& overlay = asset_overlay::GetOverlayMap();
    for (const auto& [vpath, oe] : overlay) {
        ByteSource src;
        src.kind     = ByteSource::Kind::Loose;
        src.diskPath = oe.diskPath;
        index[vpath] = std::move(src);  // overwrite the pak source if present.
        ++looseInserts;
    }

    // One build summary so the index is observable in kcdx-dev.log (§5 the
    // build is a logged lifecycle event; the test-bar dev-log observation).
    LOG_DEBUG_KV(kCat, "asset_index_built",
                 kcdx::log::KV("entries", (uint64_t)index.size()),
                 kcdx::log::KV("paks", (uint64_t)pakCount),
                 kcdx::log::KV("pak_entries", (uint64_t)pakEntryInserts),
                 kcdx::log::KV("loose", (uint64_t)looseInserts));

    return index;
}

const ByteSource* ResolveVPath(const AssetIndex& index, const std::string& vpath) {
    // The O(1) hot-path lookup (§5 "no extra hotpath checks"): normalize once,
    // one hash lookup, return the source or nullptr (a miss = the engine
    // resolves it; a later step wires the fall-through). No search, no bisection.
    const auto found = index.find(asset_overlay::NormalizeVPath(vpath));
    return found == index.end() ? nullptr : &found->second;
}

}  // namespace kcdx::fs_takeover
