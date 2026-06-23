#include "asset_index.h"

#include <atomic>      // set-once latch for the process-lifetime built index
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

// Derive a pak's BIND-ROOT — the mount point the engine binds the pak at, the
// prefix it prepends to every request for that pak's content. This is the §5
// keystone the bare-pe.name keying dropped (KI-0028 level-load abort).
//
// WHY: vanilla's OpenPack slot-7 (FUN_18193cb14) auto-derives a pak's bind-root
// as its DIRECTORY PATH (`strrchr(path,'\\')`), and the engine then requests a
// pak-resident file by `<bind-root>/<file>` — e.g. CResourceList::Load builds
// `Levels/<lvl>/auto_resourcelist.txt` and FOpen's resolver memcmp's that
// bind-root off before searching the pak's index. The data-root (Data/, Engine/)
// is a RECOGNIZED root the engine's leaf normalizer leaves un-prefixed, so the
// bind-root is the pak's dir RELATIVE TO its scan root, NOT the absolute path:
//   <root>/Tables.pak              -> bind-root ""          (top-level: bare keys)
//   <root>/Levels/kutnohorsko/*.pak-> bind-root "levels/kutnohorsko"
// INVARIANT: returned bind-root is NormalizeVPath-folded (lowercase + forward
// slash, no leading/trailing slash) so `<bind-root>/<NormalizeVPath(pe.name)>`
// composes into one already-normalized index key. A top-level pak returns "" →
// the key is bare pe.name, exactly as before (engine paks under Engine/*.pak keep
// their content-rooted `config/…`/`shaders/…` keys, so the %engine% / gameshaders
// folds still land — verified collision-safe: bind-root keying drops cross-pak
// collisions 448->182, KI-0028 _collision_check.txt).
std::string BindRootOf(const std::wstring& pakPath, const std::wstring& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // The pak's directory relative to the scan root (Data/ or Engine/).
    const fs::path rel = fs::relative(fs::path(pakPath).parent_path(),
                                      fs::path(root), ec);
    if (ec) {
        // relative() failed (cross-volume / malformed) — fall back to NO
        // bind-root (bare keys, the pre-fix behavior) rather than crash or guess.
        // Logged by the caller's per-pak path; a bare key still resolves a
        // top-level pak correctly and only mis-keys a nested one (degraded, not
        // wrong-serve) — and this path is not expected for the in-tree vanilla set.
        return std::string();
    }
    std::string s = rel.string();
    if (s == ".") return std::string();  // pak sits directly in root → no prefix.
    // Fold to the index-key form: NormalizeVPath gives lowercase + '/'-separators
    // + collapsed slashes; trim any leading/trailing slash so the join is exactly
    // one '/' between bind-root and entry name.
    s = asset_overlay::NormalizeVPath(s);
    while (!s.empty() && s.front() == '/') s.erase(0, 1);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// Walk ONE vanilla-pak root: discover every `<root>/*.pak`, CDR-parse each, and
// insert every entry as a Pak ByteSource keyed by the normalized vpath. Shared
// by every vanilla root the index covers — `<game>/Data` AND `<game>/Engine`
// (design §5: the index covers the FULL vanilla-pak set the engine draws from,
// so an engine-pak-resident file like %engine%/config/engine_core.thread_config
// is an index HIT kcdx serves, not a miss the engine fatals on — KI-0026).
// `pakCount`/`pakEntryInserts` accumulate ACROSS roots (the caller passes the
// same counters per root) so the build summary totals every root walked.
void IndexPakRoot(AssetIndex& index, const std::wstring& root,
                  size_t& pakCount, size_t& pakEntryInserts) {
    namespace fs = std::filesystem;

    // Discover this root's pak set by enumerating <root>/*.pak (the §5
    // `assumes` vanilla-pak discovery, resolved as a checkable directory
    // enumeration — not a runtime-mechanism probe; no hardcoded pak list).
    // Same std::filesystem iteration pattern mod_absorb/enabled_list_builder.cpp
    // uses for mod paks.
    // RECURSIVE walk (KI-0028): the level component paks live NESTED under
    // Data/Levels/<level>/*.pak (and other vanilla pak trees nest too) — a
    // single-level directory_iterator missed them entirely. recursive_directory_
    // iterator covers the FULL nested vanilla pak set (kcdx IS the filesystem — it
    // owns every vanilla pak the engine reads, not just the top-level ones).
    //
    // Discovery is necessary but NOT sufficient (KI-0028 root cause): a nested
    // pak's central-directory keys are bare/content-rooted (level.pak stores
    // `leveldata.xml`, `terrain/…`), but the engine requests them by the pak's
    // BIND-ROOT-prefixed path (`Levels/<lvl>/leveldata.xml` — the mount point
    // vanilla's OpenPack auto-derives from the pak's dir). Keying by bare pe.name
    // dropped that prefix, so a discovered level pak STILL missed every request →
    // the level-info loader read nothing → CreateInstance's empty-record gate
    // aborted the load (black screen). The per-pak BindRootOf (below) supplies the
    // prefix: each entry is keyed <bind-root>/<NormalizeVPath(pe.name)>, depth-
    // and mount-aware, not just depth-discovered.
    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        // This root does not resolve (wrong path / absent on this machine — an
        // absent Engine dir is NOT fatal, same as an absent Data dir). Log +
        // continue: the index is still well-formed (this root contributes no
        // vanilla sources); the other root(s) + overlay still build. Never a
        // crash, never a silent empty list passed off as whole.
        LOG_ERROR_KV(kCat, "pak_root_open_failed",
                     kcdx::log::KV("root",
                         fs::path(root).string()),
                     kcdx::log::KV("error", ec.message()),
                     kcdx::log::KV::BareStr("detail",
                         "a vanilla pak root did not open — index built with no "
                         "vanilla pak sources from THIS root (other roots + "
                         "overlay sources still ingested)"));
        return;
    }

    for (const fs::directory_entry& entry : it) {
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec) continue;
        const std::wstring ext = entry.path().extension().wstring();
        if (_wcsicmp(ext.c_str(), L".pak") != 0) continue;

        const std::wstring pakPath = entry.path().wstring();
        // The pak's bind-root (mount point) — prepended to every entry key so a
        // nested pak's files resolve under the path the engine requests them by
        // (KI-0028: Levels/<lvl>/<file>). Empty for a top-level pak (bare keys).
        const std::string bindRoot = BindRootOf(pakPath, root);
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
            src.offset     = pe.local_header_offset;
            src.size       = pe.uncompressed_size;
            src.compressed = pe.compressed_size;  // the byte count ReadPakEntry reads (DEFLATE != size)
            src.method     = pe.method;
            src.crc        = pe.crc32;
            // LAST-pak-wins on a vanilla-vs-vanilla collision: operator[]
            // INSERTS-OR-OVERWRITES, so a later pak in iteration order (and a
            // later ROOT — Engine after Data, since the caller walks Data then
            // Engine) replaces an earlier one at the same BIND-ROOT-prefixed key.
            // The bind-root keying makes most apparent collisions disappear: a
            // full-vanilla-set scan found 448 BARE-key cross-pak collisions but
            // only 182 under bind-root keying (KI-0028 _collision_check.txt) — the
            // prefix disambiguates by mount point (e.g. each level's
            // `terrain/svo/*.idx` no longer collides with the global svo.pak). The
            // 182 residual are genuine vanilla duplicates (ShaderCache.pak ≡
            // ShadersBin.pak shader-cache entries) — vanilla resolves those by
            // mount order too, so LAST-pak-wins is the correct deterministic
            // fallback (mirroring CryEngine's later-pak-overrides), not a silent
            // nondeterministic one.
            // Key = <bind-root>/<normalized entry name>. The bind-root supplies
            // the mount-point prefix vanilla's OpenPack auto-derives from the
            // pak's dir (KI-0028): a top-level pak's empty bind-root yields the
            // bare normalized name (unchanged); a nested pak (Data/Levels/<lvl>/)
            // yields `levels/<lvl>/<name>`, the form the engine requests it by.
            std::string key = asset_overlay::NormalizeVPath(pe.name);
            if (!bindRoot.empty()) key.insert(0, bindRoot + "/");
            index[std::move(key)] = std::move(src);
            ++pakEntryInserts;
        }
    }
}

}  // namespace

AssetIndex BuildAssetIndex(const std::wstring& gameDataDir,
                           const std::wstring& engineDir) {
    AssetIndex index;

    // ---- 1. Vanilla paks: discover + CDR-parse EVERY vanilla root. ----------
    //
    // The index covers the FULL vanilla-pak set the engine reads — `<game>/Data`
    // (game-data + mod paks) AND `<game>/Engine` (the engine's own archives:
    // Engine.pak, Shaders.pak, ShadersBin.pak, ShaderCache.pak,
    // ShaderCacheStartup.pak, and any future engine pak, discovered by
    // enumeration — design §5, v1.8). Indexing only Data MISSED the engine's
    // own config/shader files (e.g. %engine%/config/engine_core.thread_config in
    // Engine.pak): the miss arm _wfopen'd a non-existent loose path, the open
    // failed, and the engine raised CSystem::FatalError(0xC8) at graphics-init
    // (KI-0026). Covering Engine/ makes every engine-pak file an index HIT kcdx
    // serves through its own PKZIP/DEFLATE reader. An empty engineDir means a
    // caller that only indexes Data (the standalone tests) — Engine is skipped,
    // the Data walk is unchanged.
    //
    // The per-root walk (discover → CDR-parse → insert, LAST-pak-wins, one bad
    // pak logged + skipped) is IndexPakRoot; the counters accumulate across
    // roots so the build summary totals every root.
    size_t pakCount = 0;
    size_t pakEntryInserts = 0;
    size_t rootsWalked = 0;

    if (!gameDataDir.empty()) {
        IndexPakRoot(index, gameDataDir, pakCount, pakEntryInserts);
        ++rootsWalked;
    }
    if (!engineDir.empty()) {
        IndexPakRoot(index, engineDir, pakCount, pakEntryInserts);
        ++rootsWalked;
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
    // `roots` makes clear how many vanilla roots were walked (2 = Data + Engine
    // at the seat; 1 = a Data-only standalone-test build) so a boot log shows
    // the Engine root was indexed (KI-0026).
    LOG_DEBUG_KV(kCat, "asset_index_built",
                 kcdx::log::KV("entries", (uint64_t)index.size()),
                 kcdx::log::KV("roots", (uint64_t)rootsWalked),
                 kcdx::log::KV("paks", (uint64_t)pakCount),
                 kcdx::log::KV("pak_entries", (uint64_t)pakEntryInserts),
                 kcdx::log::KV("loose", (uint64_t)looseInserts));

    return index;
}

// kcdx OWNS the engine's pak-alias namespaces → fold an aliased index-lookup key
// to the pak-root-relative key the entries are STORED under. The engine opens a
// pak-resident file by an ALIASED path; the index keys it by the pak's own
// (NormalizeVPath-folded) entry name. Without the fold the lookup keeps the alias,
// misses the stored key, the miss arm thunks to a non-existent loose path, and the
// engine fails to load the file (KI-0026 fatal at graphics-init; KI-0028 black
// frames from un-served shaders). kcdx, as the totalizing FS owner, resolves every
// alias the engine uses — this is the alias chokepoint.
//
// Two aliases are folded (each verified against the on-disk pak central directory):
//
//   1. %engine%/X → X            (DROP the prefix). Engine.pak entries are keyed
//      pak-relative (e.g. `config/engine_core.thread_config`); the engine opens
//      them as `%engine%/config/...`. (KI-0026.)
//
//   2. data/gameshaders/X → shaders/X   (REPLACE the prefix). Shaders.pak entries
//      are stored under `shaders/` (verified: 21 `shaders/*.ext` + 180
//      `shaders/hwscripts/cryfx/*.cfx` in the pak CD), but the engine's shader
//      subsystem opens them via the `data/gameshaders/` alias (`runtime.ext`,
//      `scaleform4.ext`, `hwscripts/cryfx/posteffects.cfx`, …). Without this fold
//      EVERY shader lookup in the alias form missed → loose-open errno=2 → the
//      shader never loaded. A real alias-miss sub-case (the shader subsystem
//      could not find its shaders) — fixed here. The single prefix swap covers
//      BOTH the `.ext` and the `hwscripts/cryfx/*.cfx` families (both live under
//      `shaders/` in the pak).
//
//      NOTE: an EARLIER investigation labeled this fold "This IS KI-0028". The
//      end-to-end KI-0028 root cause (the level-load abort: black screen, no
//      input) is the BIND-ROOT keying gap (a nested level pak's entries were
//      keyed by bare pe.name, missing the `Levels/<lvl>/` prefix the engine
//      requests them by — BindRootOf above; ROOT-CAUSE-bind-root-prefix.md). This
//      gameshaders fold is a real but SEPARATE alias sub-case, not the abort
//      cause; both are correct and both ship.
//
// INVARIANT: applied to the already-NormalizeVPath'd key (lowercase + forward
// slash), so the literals to match are the folded forms (`%`, `/` unchanged by the
// fold; `data/gameshaders/` is lowercase). A key matching no alias is returned
// unchanged; the fold never swallows — a folded key that still misses falls through
// to the existing miss arm exactly as before. Allocation-light: a prefix check that
// rewrites the same string in place, on the hot lookup path. NOT folded into
// NormalizeVPath itself — that fold is shared by the overlay resolver + the
// overlay-map build and must stay a pure case+slash fold (the shared-key contract);
// the alias fold is an INDEX-key concern only.
namespace {
constexpr const char kEngineAlias[] = "%engine%/";
constexpr size_t kEngineAliasLen = sizeof(kEngineAlias) - 1;  // exclude the NUL

constexpr const char kGameShadersAlias[] = "data/gameshaders/";
constexpr size_t kGameShadersAliasLen = sizeof(kGameShadersAlias) - 1;
constexpr const char kShadersRoot[] = "shaders/";  // the pak-stored prefix
}  // namespace

void FoldEngineAliasToIndexKey(std::string& key) {
    if (key.compare(0, kEngineAliasLen, kEngineAlias) == 0) {
        key.erase(0, kEngineAliasLen);  // %engine%/X -> X (pak-root-relative)
        return;
    }
    // data/gameshaders/X -> shaders/X (the Shaders.pak stored prefix; KI-0028).
    // A prefix REPLACE (not a strip): erase `data/gameshaders/`, prepend
    // `shaders/`. The mutually-exclusive `return` above means a key reaches here
    // only if it was not the %engine% alias.
    if (key.compare(0, kGameShadersAliasLen, kGameShadersAlias) == 0) {
        key.replace(0, kGameShadersAliasLen, kShadersRoot);
    }
}

const ByteSource* ResolveVPath(const AssetIndex& index, const std::string& vpath) {
    // The O(1) hot-path lookup (§5 "no extra hotpath checks"): normalize once,
    // fold the engine's pak aliases to the stored pak-root key (%engine% KI-0026,
    // data/gameshaders KI-0028), one hash lookup, return the source or nullptr (a
    // miss = the engine resolves it via the fall-through). No search, no bisection.
    std::string key = asset_overlay::NormalizeVPath(vpath);
    FoldEngineAliasToIndexKey(key);
    const auto found = index.find(key);
    return found == index.end() ? nullptr : &found->second;
}

namespace {

// The process-lifetime built index. Filled once by SetBuiltIndex at the seat,
// read on every file open by the slot impls (next sub-step) for the life of the
// process. A function-local static so its storage outlives every caller and is
// never freed — the same process-lifetime ownership g_kcdxVtable requires (the
// engine dispatches file calls through the swapped object forever, so what those
// calls read can never be reclaimed). Empty until SetBuiltIndex runs (a lookup
// simply misses — no crash).
AssetIndex& BuiltIndexStore() {
    static AssetIndex* g_builtIndex = new AssetIndex();  // never freed — process lifetime
    return *g_builtIndex;
}

// Set-once latch — SetBuiltIndex takes ownership on the first call only.
std::atomic<bool> g_builtIndexSet{false};

}  // namespace

void SetBuiltIndex(AssetIndex&& index) {
    bool expected = false;
    if (!g_builtIndexSet.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
        // Already set this session — the seat latches its build, so this is the
        // defensive no-op path. Do NOT overwrite the live store a reader may be
        // dispatching against.
        LOG_DEBUG_KV(kCat, "asset_index_set_skipped",
                     kcdx::log::KV::BareStr("detail",
                         "SetBuiltIndex called after the index was already set "
                         "this session — keeping the first (live) index, "
                         "ignoring the duplicate build"));
        return;
    }
    BuiltIndexStore() = std::move(index);
}

const AssetIndex& GetBuiltIndex() {
    return BuiltIndexStore();
}

}  // namespace kcdx::fs_takeover
