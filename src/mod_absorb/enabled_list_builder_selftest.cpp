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

    // Synthetic resolved set:
    //   pak "zeta"  : after_game, priority 0, orderIndex 0  -> 1st
    //   pak "alpha" : after_game, priority 0, orderIndex 1  -> 2nd (index beats name)
    //   pak "gone"  : DISABLED via load_order.toml enabled=false -> EXCLUDED
    //   plugin "p_one" : after_game, priority 50 -> after the pak block
    // Expected enabled-list order: [mods.zeta, mods.alpha, p_one], count 3.
    ClearRegistry();
    Registry().push_back(MakePak("zeta",  0));
    Registry().push_back(MakePak("alpha", 1));
    Registry().push_back(MakePak("gone",  2));

    kcdx::plugins::g_manifests.clear();
    kcdx::plugins::g_manifests.push_back(MakePlugin("p_one", "Y:/plugins/p_one"));

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
    };

    // ========================================================================
    // Assertion 2 (COUNT): the disabled "mods.gone" is EXCLUDED; the list has
    // exactly 3 entries (2 enabled pak mods + 1 plugin), and the void* array
    // length == the diagnostic-entry length.
    // [broken: IsPluginEnabled gate not honored -> "gone" included -> count 4 ->
    //  FAIL; a null record inserted instead of dropped -> length mismatch -> FAIL]
    // ========================================================================
    if (list.size() != 3 || entries.size() != 3) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: rebuilt list has %zu entries (diag entries %zu), expected 3 "
            "(2 enabled pak mods + 1 plugin; 'mods.gone' is disabled and must be "
            "EXCLUDED) — the IsPluginEnabled gate is not honored, or a record "
            "was inserted/dropped wrongly", list.size(), entries.size());
        cleanup();
        Fail(reason);
        return;
    }

    // ========================================================================
    // Assertion 3 (ORDER): resolved load order. zeta (orderIndex 0) before
    // alpha (orderIndex 1) — orderIndex beats name; both pak mods (priority 0)
    // before the plugin (priority 50).
    // [broken: not sorted by the (zone,priority,orderIndex,name) key -> wrong
    //  order -> FAIL; plugin not after the pak block -> FAIL]
    // ========================================================================
    {
        const bool orderOk =
            entries[0].loadOrderName == "mods.zeta" &&
            entries[1].loadOrderName == "mods.alpha" &&
            entries[2].loadOrderName == "p_one" &&
            entries[0].isPlugin == false &&
            entries[1].isPlugin == false &&
            entries[2].isPlugin == true;
        if (!orderOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: rebuilt order = [%s, %s, %s], expected [mods.zeta, "
                "mods.alpha, p_one] — the list must sort by the load-order key "
                "(zone, priority, orderIndex, name): orderIndex 0 before 1 "
                "(despite 'zeta' > 'alpha'), and the priority-0 pak block before "
                "the priority-50 plugin",
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
    // folder "X:\fake\mods\zeta" -> "X:\fake\mods\zeta/"; the plugin's
    // "Y:/plugins/p_one" -> "Y:\plugins\p_one/".
    // [broken: path not normalized -> forward slashes survive -> FAIL; wrong
    //  field copied -> FAIL]
    // ========================================================================
    {
        const bool pathOk =
            entries[0].rootPathSlash == "X:\\fake\\mods\\zeta/" &&
            entries[2].rootPathSlash == "Y:\\plugins\\p_one/";
        if (!pathOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: rebuilt path fields wrong — mods.zeta=\"%s\" (expected "
                "\"X:\\fake\\mods\\zeta/\"), p_one=\"%s\" (expected "
                "\"Y:\\plugins\\p_one/\") — each record's +0x08 path must be the "
                "mod's folder normalized to the native form (backslash body + "
                "trailing '/')",
                entries[0].rootPathSlash.c_str(),
                entries[2].rootPathSlash.c_str());
            cleanup();
            Fail(reason);
            return;
        }
    }

    cleanup();

    std::snprintf(reason, sizeof(reason),
        "NormalizeToNativeRecordForm maps a mixed-separator input to the native "
        "form (backslash body + trailing '/'); BuildEnabledList excludes the "
        "disabled mod (count 3, not 4), orders by the (zone,priority,orderIndex,"
        "name) key (mods.zeta[0] before mods.alpha[1] despite name; both pak mods "
        "before the priority-50 plugin), and each record's path field is the "
        "normalized folder. Live state restored. (Live MOUNT end-to-end is the "
        "verification checkpoint.)");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-55 enabled-list-builder");
}

}  // namespace kcdx::mod_absorb
