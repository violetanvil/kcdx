#include "mod_absorb_e2e_selftest.h"

#include <climits>
#include <cstdio>    // snprintf
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "pak_mod_registry.h"
#include "../load_order.h"
#include "../paths.h"
#include "../test.h"

// cap-57 end-to-end self-test — the permanent regression net over the
// discovery -> registry -> load-order behaviors of the mod-loader absorb that
// are queryable at boot. Each assertion is FALSIFIABLE and names the broken
// state it catches (a non-falsifiable PASS proves nothing).

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-57-mod-absorb-e2e";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-57 mod-absorb-e2e");
}

// A synthetic pak mod for the isolated resolved-order assertion.
PakMod MakeMod(const std::string& id, int orderIdx) {
    PakMod m;
    m.modId = id;
    m.rootPathNoSlash = "X:/fake/mods/" + id;
    m.rootPathSlash = m.rootPathNoSlash + "/";
    m.manifest.ok = true;
    m.modOrderIndex = orderIdx;
    m.fromModsDir = true;
    return m;
}

}  // namespace

void RunModAbsorbE2ESelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;

    char reason[1280];

    // ========================================================================
    // Assertion 1 (LIVE state, READ-ONLY; robust to an empty mods/): the
    // discovery -> registry -> load-order-fold contract on the ACTUAL live
    // resolved state this boot produced. By the first update tick, Discover +
    // load_order::Resolve + ApplyVersionGate have all run, so the live state is
    // final. For EVERY fromModsDir pak mod the live scan registered, assert:
    //   - LoadOrderNameFor(modId) == "mods.<modId>" (the key is composed in one
    //     place; a drift breaks every downstream lookup);
    //   - Of("mods.<modId>") returns a REAL fold row (priority == 0, the
    //     after_game pak block), NOT the unknown-name default sentinel
    //     (priority 50) — i.e. the fold actually created the row;
    //   - IsPluginEnabled("mods.<modId>") is true (a compatible mod loads).
    // This is vacuously true if the live mods/ is empty (no fromModsDir mods to
    // check) and REAL coverage the moment a vanilla pak mod is present — so the
    // suite passes on a clean install yet catches a real discovery/fold break on
    // a populated one.
    // [broken: discovery didn't register mods/ mods, or the fold didn't create
    //  the "mods.<modid>" row -> Of() returns the default sentinel (priority 50)
    //  -> FAIL; the live gate wrongly disabled a compatible mod or the
    //  LoadOrderNameFor key drifted -> FAIL]
    // ========================================================================
    {
        const std::vector<PakMod>& reg = Registry();
        size_t fromModsCount = 0;
        for (const PakMod& m : reg) {
            if (!m.fromModsDir) continue;
            ++fromModsCount;
            const std::string name = LoadOrderNameFor(m.modId);
            const kcdx::load_order::Effective& eff = kcdx::load_order::Of(name);
            const bool nameOk = (name == "mods." + m.modId);
            // priority 0 == the real fold row; priority 50 == the unknown-name
            // default sentinel (Of() returns that for a name with no row).
            const bool foldedRow =
                eff.zone == kcdx::load_order::Zone::AfterGame &&
                eff.priority == 0;
            const bool enabled = kcdx::load_order::IsPluginEnabled(name);
            if (!(nameOk && foldedRow && enabled)) {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: live fromModsDir pak mod \"%s\" did not resolve — "
                    "LoadOrderNameFor=\"%s\" (expected \"mods.%s\"), "
                    "Of() zone=%d priority=%d (expected after_game(%d) priority 0 "
                    "— priority 50 = the unknown-name default sentinel, i.e. the "
                    "fold never created the row), IsPluginEnabled=%d (expected 1, "
                    "a compatible mod loads). The discovery -> registry -> "
                    "load-order fold is broken for a real vanilla mod.",
                    m.modId.c_str(), name.c_str(), m.modId.c_str(),
                    static_cast<int>(eff.zone), eff.priority,
                    static_cast<int>(kcdx::load_order::Zone::AfterGame),
                    enabled ? 1 : 0);
                Fail(reason);
                return;
            }
        }
        // fromModsCount is informational (it varies with the install). The
        // assertion is the per-mod resolvability above — vacuous when 0.
        (void)fromModsCount;
    }

    // ------------------------------------------------------------------------
    // Assertions 2 + 3 MUTATE the registry + the global load_order state.
    // Capture the FULL live state verbatim (registry deep-copy + the load_order
    // Snapshot, engineAccepted verdicts included) so the live resolved state —
    // which the SELECT-detour takeover already consumed this boot and which
    // assertion 1 just read — is put back EXACTLY. A bare Resolve() would NOT
    // restore it (it resets every engineAccepted to true), so we restore the
    // snapshot directly.
    // ------------------------------------------------------------------------
    const kcdx::load_order::Snapshot savedLoadOrder = kcdx::load_order::CaptureState();
    std::vector<PakMod> savedRegistry = Registry();  // deep copy

    auto restore = [&]() {
        ClearRegistry();
        Registry() = savedRegistry;
        kcdx::load_order::RestoreState(savedLoadOrder);
    };

    // ========================================================================
    // Assertion 2 (SYNTHETIC root): the SUPERSET marker-file classification —
    // "a kcdx plugin works dropped in EITHER dir; a kcdx.toml folder is NOT
    // registered as a vanilla pak mod." Driven against a SYNTHETIC temp root so
    // it does not depend on a physical kcdx plugin being installed in the live
    // mods/ (which the user controls). Lay out:
    //     <tmp>/vanilla_pak/mod.manifest   (no kcdx.toml -> register as a PakMod)
    //     <tmp>/kcdx_in_mods/kcdx.toml     (has kcdx.toml -> SKIP; the plugin
    //                                       walker owns it, even in mods/)
    // After Discover(<tmp>, fromModsDir=true): the registry contains a PakMod
    // for "vanilla_pak" and NONE for "kcdx_in_mods" — proving the kcdx.toml
    // folder was classified as a plugin (skipped), not double-registered as a
    // pak mod.
    // [broken: a kcdx.toml folder wrongly registered as a pak mod -> it appears
    //  in Registry() under "mods.kcdx_in_mods" -> FAIL; the mod.manifest folder
    //  not registered -> the classifier skipped a real vanilla mod -> FAIL]
    // ========================================================================
    {
        std::error_code ec;
        const std::filesystem::path tmpRoot =
            std::filesystem::temp_directory_path(ec) / "kcdx_cap57_e2e_root";
        std::filesystem::remove_all(tmpRoot, ec);  // clean any prior run
        std::filesystem::create_directories(tmpRoot / "vanilla_pak", ec);
        std::filesystem::create_directories(tmpRoot / "kcdx_in_mods", ec);
        {
            std::ofstream m(tmpRoot / "vanilla_pak" / "mod.manifest",
                            std::ios::binary | std::ios::trunc);
            m << "<kcd_mod><info><name>Vanilla Pak</name>"
                 "<modid>vanilla_pak</modid></info></kcd_mod>\n";
        }
        {
            // A kcdx plugin physically in a mods/-style root. Its kcdx.toml
            // presence MUST classify it as a plugin (skipped by Discover).
            std::ofstream t(tmpRoot / "kcdx_in_mods" / "kcdx.toml",
                            std::ios::binary | std::ios::trunc);
            t << "[plugin]\nname = \"kcdx_in_mods\"\n";
        }

        ClearRegistry();
        Discover(tmpRoot, /*fromModsDir=*/true);

        bool vanillaRegistered = false;
        bool kcdxFolderRegistered = false;
        for (const PakMod& m : Registry()) {
            if (m.modId == "vanilla_pak") vanillaRegistered = true;
            if (m.modId == "kcdx_in_mods") kcdxFolderRegistered = true;
        }
        std::filesystem::remove_all(tmpRoot, ec);

        if (!vanillaRegistered || kcdxFolderRegistered) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: marker-file classification wrong — vanilla_pak "
                "(mod.manifest, no kcdx.toml) registered=%d (expected 1), "
                "kcdx_in_mods (has kcdx.toml) registered=%d (expected 0). A "
                "kcdx.toml folder MUST be classified as a plugin and SKIPPED by "
                "Discover (the plugin walker owns it, even dropped in mods/) — "
                "never double-registered as a vanilla pak mod; a mod.manifest "
                "folder MUST register.",
                vanillaRegistered ? 1 : 0, kcdxFolderRegistered ? 1 : 0);
            restore();
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 3 (ISOLATED global): resolved-order sanity — the "mods.<modid>"
    // rows fold into the after_game zone at priority 0 (the pak-mod block) and
    // preserve the mod_order.txt RELATIVE order via the orderIndex tiebreaker.
    // Two synthetic mods whose names sort OPPOSITE to their mod_order.txt order
    // (modOrderIndex 0 = "zeta", index 1 = "alpha") must resolve so the
    // mod_order order wins over the name: zeta (index 0) sorts before alpha
    // (index 1). Replicate the engine sort tuple (zone, priority, orderIndex,
    // name) over the FOLDED Effective rows.
    // [broken: the fold lost the zone/priority -> not after_game/0 -> FAIL; the
    //  orderIndex tiebreaker dropped -> alpha sorts before zeta on name ->
    //  mod_order order NOT preserved -> FAIL]
    // ========================================================================
    {
        ClearRegistry();
        Registry().push_back(MakeMod("zeta",  0));  // mod_order line 0
        Registry().push_back(MakeMod("alpha", 1));  // mod_order line 1
        // Resolve over a NO-OVERRIDE state so the fold defaults stand. Read("")
        // / a nonexistent path is a no-op (author defaults), then Resolve folds.
        kcdx::load_order::Read(
            kcdx::paths::EngineDataDirPath() / L"kcdx_cap57_absent.toml");
        kcdx::load_order::Resolve();

        const kcdx::load_order::Effective& effZeta =
            kcdx::load_order::Of("mods.zeta");
        const kcdx::load_order::Effective& effAlpha =
            kcdx::load_order::Of("mods.alpha");

        const bool zoneOk =
            effZeta.zone == kcdx::load_order::Zone::AfterGame &&
            effZeta.priority == 0 && effZeta.orderIndex == 0 &&
            effAlpha.zone == kcdx::load_order::Zone::AfterGame &&
            effAlpha.priority == 0 && effAlpha.orderIndex == 1;

        // The engine sort key (zone, priority, orderIndex, name). zeta(idx 0)
        // must sort BEFORE alpha(idx 1) even though "zeta" > "alpha" by name —
        // the mod_order.txt relative order is preserved.
        auto key = [](const kcdx::load_order::Effective& e,
                      const std::string& name) {
            return std::tuple<int, int, int, std::string>{
                static_cast<int>(e.zone), e.priority, e.orderIndex, name};
        };
        const bool zetaBeforeAlpha =
            key(effZeta, "mods.zeta") < key(effAlpha, "mods.alpha");

        if (!(zoneOk && zetaBeforeAlpha)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: resolved-order sanity — zeta{zone=%d,prio=%d,idx=%d} "
                "alpha{zone=%d,prio=%d,idx=%d}, zetaBeforeAlpha=%d (expected both "
                "after_game(%d)/priority 0, idx 0 and 1, and zeta before alpha — "
                "the mod_order.txt relative order beats the name tiebreak). The "
                "fold lost the zone/priority or the orderIndex tiebreaker.",
                static_cast<int>(effZeta.zone), effZeta.priority,
                effZeta.orderIndex, static_cast<int>(effAlpha.zone),
                effAlpha.priority, effAlpha.orderIndex,
                zetaBeforeAlpha ? 1 : 0,
                static_cast<int>(kcdx::load_order::Zone::AfterGame));
            restore();
            Fail(reason);
            return;
        }
    }

    // Restore the live registry + load-order state EXACTLY.
    restore();

    std::snprintf(reason, sizeof(reason),
        "End-to-end: EVERY live fromModsDir pak mod resolves to its "
        "\"mods.<modid>\" row (after_game/priority-0, IsPluginEnabled true) — "
        "vacuous on an empty mods/, real on a populated one; the SUPERSET "
        "marker-file classification (Discover over a synthetic root) registers a "
        "mod.manifest folder as a pak mod and SKIPS a kcdx.toml folder (a plugin, "
        "not double-registered); folded pak mods land in the after_game/priority-0 "
        "block and preserve mod_order.txt relative order via the orderIndex "
        "tiebreaker (zeta[0] before alpha[1] despite name). Live state restored. "
        "(The native MOUNT of every enabled record, in kcdx order, is the "
        "verification checkpoint — not asserted here.)");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-57 mod-absorb-e2e");
}

}  // namespace kcdx::mod_absorb
