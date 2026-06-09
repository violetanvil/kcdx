#include "enabled_list_builder_selftest.h"

#include <cstdio>   // snprintf
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "enabled_list_builder.h"
#include "pak_mod_registry.h"
#include "../load_order.h"
#include "../paths.h"
#include "../plugin_loader.h"
#include "../test.h"

// cap-55 self-test — enabled-list builder. Each assertion is falsifiable + names
// the broken state it catches (a non-falsifiable PASS proves nothing). The
// order/count/path assertions drive the GLOBAL resolved state (registry +
// g_manifests + load_order) in isolation and RESTORE it verbatim before
// returning, so the live boot state is untouched.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-55-enabled-list-builder";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-55 enabled-list-builder");
}

PakMod MakePak(const std::string& id, int orderIdx) {
    PakMod m;
    m.modId          = id;
    m.rootPathNoSlash = "X:\\fake\\mods\\" + id;
    m.rootPathSlash   = m.rootPathNoSlash + "/";
    m.manifest.ok     = true;
    m.modOrderIndex   = orderIdx;
    m.fromModsDir     = true;
    return m;
}

kcdx::plugins::PluginManifest MakePlugin(const std::string& name,
                                         const std::string& folder) {
    kcdx::plugins::PluginManifest m;
    m.name        = name;
    m.folderPath  = std::filesystem::path(folder);
    m.defaultPosition = "after_game";
    m.defaultPriority = 50;  // sorts AFTER pak mods (priority 0) in after_game.
    return m;
}

}  // namespace

void RunEnabledListSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;

    char reason[1024];

    // ========================================================================
    // Assertion 1 (UNIT, pure): path normalization. A mixed-separator input
    // ("X:/fake\\mods/over/") must normalize to the NATIVE record form —
    // backslash body, exactly one trailing '/' for the slash form, no trailing
    // separator for the no-slash form.
    // [broken: '/' not converted to '\' -> mixed body -> FAIL; trailing not
    //  normalized -> double '/' or missing '/' -> FAIL]
    // ========================================================================
    {
        std::string slash, noSlash;
        NormalizeToNativeRecordForm("X:/fake\\mods/over/", slash, noSlash);
        const bool ok = (noSlash == "X:\\fake\\mods\\over") &&
                        (slash   == "X:\\fake\\mods\\over/");
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: NormalizeToNativeRecordForm(\"X:/fake\\\\mods/over/\") = "
                "noSlash=\"%s\" slash=\"%s\" (expected noSlash="
                "\"X:\\fake\\mods\\over\" slash=\"X:\\fake\\mods\\over/\") — "
                "the native record form is a backslash body + a single trailing "
                "forward '/' (slash) / no trailing separator (noSlash)",
                noSlash.c_str(), slash.c_str());
            Fail(reason);
            return;
        }
    }

    // ------------------------------------------------------------------------
    // Assertions 2-4 drive the GLOBAL state. Capture the live load_order state,
    // registry, AND g_manifests verbatim; run an isolated Read+Resolve over a
    // synthetic set; assert; then RESTORE all three exactly.
    // ------------------------------------------------------------------------
    const kcdx::load_order::Snapshot savedLoadOrder = kcdx::load_order::CaptureState();
    std::vector<PakMod> savedRegistry = Registry();                 // deep copy
    std::vector<kcdx::plugins::PluginManifest> savedManifests =
        kcdx::plugins::g_manifests;                                 // deep copy

    auto restore = [&]() {
        ClearRegistry();
        Registry() = savedRegistry;
        kcdx::plugins::g_manifests = savedManifests;
        kcdx::load_order::RestoreState(savedLoadOrder);
    };

    // A pak-bearing plugin needs a REAL folder with a Data/*.pak so
    // PluginShipsPak (the KI-0012 gate) returns true. A pak-LESS plugin points
    // at a folder with no Data/*.pak so the gate returns false. Build both under
    // the engine-data dir; cleanup removes them.
    const std::filesystem::path tmpRoot =
        kcdx::paths::EngineDataDirPath() / L"kcdx_cap55_tmp_plugins";
    const std::filesystem::path pakPluginDir  = tmpRoot / L"p_pak";
    const std::filesystem::path pakDataDir     = pakPluginDir / L"Data";
    const std::filesystem::path barePluginDir = tmpRoot / L"p_bare";
    {
        std::error_code ec;
        std::filesystem::create_directories(pakDataDir, ec);
        std::filesystem::create_directories(barePluginDir, ec);  // NO Data/ → pak-less.
        std::ofstream(pakDataDir / L"assets.pak",
                      std::ios::binary | std::ios::trunc).put('P');  // a real *.pak file.
    }

    // Synthetic resolved set (KI-0012 fix — the engine MOUNT list is the
    // pak-mountable SUBSET of the unified order):
    //   pak "zeta"  : after_game, priority 0, orderIndex 0  -> 1st (mounts)
    //   pak "alpha" : after_game, priority 0, orderIndex 1  -> 2nd (mounts; index beats name)
    //   pak "gone"  : DISABLED via load_order.toml enabled=false -> EXCLUDED
    //   plugin "p_pak"  : after_game, priority 50, SHIPS Data/*.pak -> in the list, after the paks
    //   plugin "p_bare" : after_game, priority 60, NO pak -> EXCLUDED from the engine list
    //                     (loads via kcdx's own loader; KI-0012 — a pak-less record
    //                     crashed graphics init).
    // Expected engine MOUNT list: [mods.zeta, mods.alpha, p_pak], count 3 (NOT p_bare).
    ClearRegistry();
    Registry().push_back(MakePak("zeta",  0));
    Registry().push_back(MakePak("alpha", 1));
    Registry().push_back(MakePak("gone",  2));

    kcdx::plugins::g_manifests.clear();
    {
        kcdx::plugins::PluginManifest pakPlugin =
            MakePlugin("p_pak", pakPluginDir.string());
        kcdx::plugins::PluginManifest barePlugin =
            MakePlugin("p_bare", barePluginDir.string());
        barePlugin.defaultPriority = 60;  // sorts AFTER p_pak — proves order, not luck.
        kcdx::plugins::g_manifests.push_back(std::move(pakPlugin));
        kcdx::plugins::g_manifests.push_back(std::move(barePlugin));
    }

    // A temp load_order.toml disabling "mods.gone".
    const std::filesystem::path tempLoadOrder =
        kcdx::paths::EngineDataDirPath() / L"kcdx_cap55_tmp_load_order.toml";
    {
        std::ofstream out(tempLoadOrder, std::ios::binary | std::ios::trunc);
        out << "[[plugin]]\n"
               "name = \"mods.gone\"\n"
               "enabled = false\n";
    }

    kcdx::load_order::Read(tempLoadOrder);
    kcdx::load_order::Resolve();

    std::vector<EnabledListEntry> entries;
    std::vector<void*> list = BuildEnabledList(&entries);

    auto cleanup = [&]() {
        restore();
        std::error_code ec;
        std::filesystem::remove(tempLoadOrder, ec);
        std::filesystem::remove_all(tmpRoot, ec);
    };

    // ========================================================================
    // Assertion 2 (COUNT + KI-0012 pak-gate): the disabled "mods.gone" is
    // EXCLUDED, AND the pak-LESS plugin "p_bare" is EXCLUDED from the engine
    // MOUNT list (it has no Data/*.pak — a pak-less record crashed graphics
    // init). The list has exactly 3 entries (2 enabled pak mods + 1 pak-bearing
    // plugin), and the void* array length == the diagnostic-entry length.
    // [broken: IsPluginEnabled gate not honored -> "gone" in -> count 4 -> FAIL;
    //  KI-0012 regression: a pak-less plugin re-enters the engine list ->
    //  "p_bare" in -> count 4 -> FAIL; a null record inserted instead of dropped
    //  -> length mismatch -> FAIL]
    // ========================================================================
    if (list.size() != 3 || entries.size() != 3) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: engine MOUNT list has %zu entries (diag entries %zu), expected "
            "3 (2 enabled pak mods + 1 pak-BEARING plugin; 'mods.gone' is disabled "
            "and the pak-LESS 'p_bare' must BOTH be EXCLUDED) — the IsPluginEnabled "
            "gate or the KI-0012 PluginShipsPak gate is not honored, or a record "
            "was inserted/dropped wrongly", list.size(), entries.size());
        cleanup();
        Fail(reason);
        return;
    }

    // ========================================================================
    // Assertion 3 (ORDER + pak-gate identity): resolved load order. zeta
    // (orderIndex 0) before alpha (orderIndex 1) — orderIndex beats name; both
    // pak mods (priority 0) before the pak-bearing plugin (priority 50). The
    // 3rd entry is "p_pak" (the pak-bearing plugin), NEVER "p_bare" (pak-less,
    // even though it sorts adjacent at priority 60) — proving the gate excludes
    // by pak-presence, not by order/count luck.
    // [broken: not sorted by the (zone,priority,orderIndex,name) key -> wrong
    //  order -> FAIL; pak-less p_bare admitted instead of p_pak -> FAIL]
    // ========================================================================
    {
        const bool orderOk =
            entries[0].loadOrderName == "mods.zeta" &&
            entries[1].loadOrderName == "mods.alpha" &&
            entries[2].loadOrderName == "p_pak" &&
            entries[0].isPlugin == false &&
            entries[1].isPlugin == false &&
            entries[2].isPlugin == true;
        if (!orderOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: engine MOUNT order = [%s, %s, %s], expected [mods.zeta, "
                "mods.alpha, p_pak] — the list must sort by the load-order key "
                "(zone, priority, orderIndex, name): orderIndex 0 before 1 "
                "(despite 'zeta' > 'alpha'), the priority-0 pak block before the "
                "pak-bearing plugin, and the pak-LESS 'p_bare' EXCLUDED (KI-0012 "
                "— only pak-mountable entries enter the engine list)",
                entries[0].loadOrderName.c_str(),
                entries[1].loadOrderName.c_str(),
                entries[2].loadOrderName.c_str());
            cleanup();
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 4 (PATH): each record's path field is the mod's normalized
    // folder path in the native form (backslash body + trailing '/'). zeta's
    // folder "X:\fake\mods\zeta" -> "X:\fake\mods\zeta/"; the pak-bearing plugin
    // p_pak's real folder normalized likewise (it is in the engine list).
    // [broken: path not normalized -> forward slashes survive -> FAIL; wrong
    //  field copied -> FAIL]
    // ========================================================================
    {
        std::string expectPakPlugin;
        {
            std::string slash, noSlash;
            NormalizeToNativeRecordForm(pakPluginDir.string(), slash, noSlash);
            expectPakPlugin = slash;
        }
        const bool pathOk =
            entries[0].rootPathSlash == "X:\\fake\\mods\\zeta/" &&
            entries[2].rootPathSlash == expectPakPlugin;
        if (!pathOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: rebuilt path fields wrong — mods.zeta=\"%s\" (expected "
                "\"X:\\fake\\mods\\zeta/\"), p_pak=\"%s\" (expected \"%s\") — each "
                "record's +0x08 path must be the mod's folder normalized to the "
                "native form (backslash body + trailing '/')",
                entries[0].rootPathSlash.c_str(),
                entries[2].rootPathSlash.c_str(), expectPakPlugin.c_str());
            cleanup();
            Fail(reason);
            return;
        }
    }

    cleanup();

    std::snprintf(reason, sizeof(reason),
        "NormalizeToNativeRecordForm maps a mixed-separator input to the native "
        "form; BuildEnabledList excludes the disabled mod AND the pak-LESS plugin "
        "'p_bare' (KI-0012 — only pak-mountable entries enter the engine MOUNT "
        "list; count 3, not 4 or 5), admits the pak-BEARING plugin 'p_pak' at its "
        "ordered position, orders by the (zone,priority,orderIndex,name) key "
        "(mods.zeta[0] before mods.alpha[1]; pak block before the plugin), and "
        "each record's path is the normalized folder. Live state restored. (Live "
        "MOUNT end-to-end is the verification checkpoint.)");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-55 enabled-list-builder");
}

}  // namespace kcdx::mod_absorb
