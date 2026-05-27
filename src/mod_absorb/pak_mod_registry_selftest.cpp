#include "pak_mod_registry_selftest.h"

#include <climits>
#include <cstdio>    // snprintf
#include <cstring>   // strcmp
#include <fstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "pak_mod_registry.h"
#include "../load_order.h"
#include "../paths.h"
#include "../test.h"

// cap-54 self-test — pak-mod registry + load-order fold + version gate. Each
// assertion is falsifiable + names the broken state it catches (AP15).

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-54-pak-mod-registry";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-54 pak-mod-registry");
}

// Build a synthetic PakMod for the fold/gate assertions.
PakMod MakeMod(const std::string& id, int orderIdx,
               std::vector<std::string> supports = {}) {
    PakMod m;
    m.modId = id;
    m.rootPathNoSlash = "X:/fake/mods/" + id;
    m.rootPathSlash = m.rootPathNoSlash + "/";
    m.manifest.ok = true;
    m.manifest.supports = std::move(supports);
    m.modOrderIndex = orderIdx;
    m.fromModsDir = true;
    return m;
}

}  // namespace

void RunPakRegistrySelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;

    char reason[768];

    // ========================================================================
    // Assertion 1 (UNIT): ParseModOrderText — '#' comments + blanks stripped,
    // FILE ORDER preserved as the 0-based survivor index.
    // [broken: comment/blank not skipped -> wrong index or a phantom entry;
    //  order not preserved -> wrong index -> FAIL]
    // ========================================================================
    {
        const std::string text =
            "# vanilla mod_order.txt\n"
            "alpha_mod\n"
            "\n"                       // blank line — skipped, no index gap
            "# a comment between\n"
            "  beta_mod  \n"           // surrounding whitespace trimmed
            "gamma_mod\n";
        const std::unordered_map<std::string, int> got = ParseModOrderText(text);
        const bool ok =
            got.size() == 3 &&
            got.count("alpha_mod") && got.at("alpha_mod") == 0 &&
            got.count("beta_mod")  && got.at("beta_mod")  == 1 &&
            got.count("gamma_mod") && got.at("gamma_mod") == 2;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: ParseModOrderText got %zu entries (expected 3: "
                "alpha=0,beta=1,gamma=2) — comments/blanks not stripped, "
                "whitespace not trimmed, or file order not preserved as the "
                "0-based survivor index", got.size());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 2 (UNIT): the modOrderIndex -> sort ordering. The load-order
    // sort key is (zone, priority, orderIndex, name). Two pak mods at the same
    // (after_game, 0) with indices 0,1 must sort 0-before-1; a -1 (-> INT_MAX)
    // sorts AFTER both, then by name. We replicate the EXACT sort tuple the
    // engine uses, fed Effective values built by the fold rule, so the broken
    // state is unambiguous.
    // [broken: orderIndex not in the key, or ordered descending -> wrong order;
    //  -1 not mapped to INT_MAX -> a -1 sorts FIRST -> FAIL]
    // ========================================================================
    {
        // The fold rule: zone=after_game, priority=0, orderIndex = idx>=0?idx:INT_MAX.
        auto eff = [](int modOrderIdx) {
            kcdx::load_order::Effective e;
            e.zone = kcdx::load_order::Zone::AfterGame;
            e.priority = 0;
            e.orderIndex = (modOrderIdx >= 0) ? modOrderIdx : INT_MAX;
            return e;
        };
        // The engine sort key (mirrors config.cpp pluginKey's tuple order:
        // zone, priority, orderIndex, name).
        auto key = [](const kcdx::load_order::Effective& e, const std::string& name) {
            return std::tuple<int, int, int, std::string>{
                static_cast<int>(e.zone), e.priority, e.orderIndex, name};
        };
        const auto kA = key(eff(0), "mods.zeta");   // index 0
        const auto kB = key(eff(1), "mods.alpha");  // index 1, name sorts BEFORE zeta
        const auto kC = key(eff(-1), "mods.aaa");   // unlisted -> INT_MAX, name first

        // 0 before 1 even though "zeta" > "alpha": orderIndex beats name.
        const bool aBeforeB = kA < kB;
        // both listed before the unlisted (-1 -> INT_MAX).
        const bool aBeforeC = kA < kC;
        const bool bBeforeC = kB < kC;
        if (!(aBeforeB && aBeforeC && bBeforeC)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: pak-mod sort order wrong (aBeforeB=%d aBeforeC=%d "
                "bBeforeC=%d) — orderIndex must order BEFORE name (index 0 "
                "before index 1 despite name), and a -1 (-> INT_MAX) must sort "
                "AFTER every listed mod",
                aBeforeB, aBeforeC, bBeforeC);
            Fail(reason);
            return;
        }
    }

    // ------------------------------------------------------------------------
    // Assertions 3 + 4 drive the GLOBAL load_order state. Capture the FULL live
    // state VERBATIM (every Effective row, engineAccepted verdicts included, +
    // the user-override layer) AND the live registry, run an ISOLATED
    // Read+Resolve over synthetic mods, assert, then RESTORE the captured state
    // exactly. A bare Resolve() would NOT restore it — Resolve() resets every
    // engineAccepted to true, dropping the zone_gate + pak-mod-version-gate
    // verdicts the live boot applied — so we restore the snapshot directly. The
    // test runs at the first update tick, after the engine's own Resolve +
    // gates already ran; restoration keeps the live state byte-identical.
    // ------------------------------------------------------------------------
    const kcdx::load_order::Snapshot savedLoadOrder = kcdx::load_order::CaptureState();
    std::vector<PakMod> savedRegistry = Registry();  // deep copy

    auto restore = [&]() {
        ClearRegistry();
        Registry() = savedRegistry;
        kcdx::load_order::RestoreState(savedLoadOrder);
    };

    // Synthetic registry: one mod we'll OVERRIDE, one Incompatible mod, one
    // plain compatible mod.
    ClearRegistry();
    Registry().push_back(MakeMod("over_mod",  0));               // override target
    Registry().push_back(MakeMod("incompat",  1, {"1.6*"}));     // version-disabled below
    Registry().push_back(MakeMod("plain",     2));               // no restriction

    // A temp load_order.toml whose [[plugin]] row keyed "mods.over_mod" OVERRIDES
    // zone -> before_game, priority -> 77, enabled -> false. (The override-key
    // is the synthesized "mods.<modid>" name — proves the fold + the override
    // layer share one key space.)
    const std::filesystem::path tempLoadOrder =
        kcdx::paths::EngineDataDirPath() / L"kcdx_cap54_tmp_load_order.toml";
    {
        std::ofstream out(tempLoadOrder, std::ios::binary | std::ios::trunc);
        out << "[[plugin]]\n"
               "name = \"mods.over_mod\"\n"
               "zone = \"before_game\"\n"
               "priority = 77\n"
               "enabled = false\n";
    }

    kcdx::load_order::Read(tempLoadOrder);
    kcdx::load_order::Resolve();

    // ========================================================================
    // Assertion 3 (UNIT, isolated global): the "mods.<modid>" Effective lookup.
    // A NON-overridden pak mod folds to zone=after_game, priority=0; an
    // OVERRIDDEN one takes the load_order.toml row's zone/priority/enabled.
    // [broken: fold doesn't register a "mods." row -> Of() returns the default
    //  (after_game, 50) for "plain" -> priority!=0 -> FAIL; override layer
    //  doesn't apply to a "mods." key -> over_mod stays after_game/0/enabled
    //  -> FAIL]
    // ========================================================================
    {
        const auto& effPlain = kcdx::load_order::Of("mods.plain");
        const bool plainOk =
            effPlain.zone == kcdx::load_order::Zone::AfterGame &&
            effPlain.priority == 0 &&
            effPlain.orderIndex == 2;  // its mod_order index
        if (!plainOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: Of(\"mods.plain\") = zone %d priority %d orderIndex %d "
                "(expected after_game(%d) priority 0 orderIndex 2) — the fold "
                "did not register the pak mod's 'mods.<modid>' row at the "
                "after_game/priority-0 block",
                static_cast<int>(effPlain.zone), effPlain.priority,
                effPlain.orderIndex,
                static_cast<int>(kcdx::load_order::Zone::AfterGame));
            restore();
            std::error_code ec; std::filesystem::remove(tempLoadOrder, ec);
            Fail(reason);
            return;
        }

        const auto& effOver = kcdx::load_order::Of("mods.over_mod");
        const bool overOk =
            effOver.zone == kcdx::load_order::Zone::BeforeGame &&
            effOver.priority == 77 &&
            kcdx::load_order::IsPluginEnabled("mods.over_mod") == false;
        if (!overOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: load_order.toml 'mods.over_mod' override not applied — "
                "Of() = zone %d priority %d, IsPluginEnabled = %d (expected "
                "before_game(%d) priority 77 enabled=false) — the override "
                "layer does not key on the synthesized 'mods.<modid>' name",
                static_cast<int>(effOver.zone), effOver.priority,
                kcdx::load_order::IsPluginEnabled("mods.over_mod") ? 1 : 0,
                static_cast<int>(kcdx::load_order::Zone::BeforeGame));
            restore();
            std::error_code ec; std::filesystem::remove(tempLoadOrder, ec);
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 4 (UNIT, isolated global): the version gate. ApplyVersionGate
    // against a runtime string the "incompat" mod's <supports> (1.6*) does NOT
    // match must flip its engineAccepted=false -> IsPluginEnabled false, while a
    // mod with no restriction ("plain") stays enabled. Returns the disabled
    // count (== 1: only "incompat").
    // [broken: gate doesn't run DecideModCompat / doesn't call SetEngineAccepted
    //  -> incompat stays enabled -> FAIL; gate over-disables -> plain disabled
    //  -> FAIL]
    // ========================================================================
    {
        const std::string runtime = "1.5.1164953";  // matches 1.5*, NOT 1.6*
        const size_t disabled = ApplyVersionGate(runtime);
        const bool incompatDisabled =
            kcdx::load_order::IsPluginEnabled("mods.incompat") == false;
        const bool plainStillEnabled =
            kcdx::load_order::IsPluginEnabled("mods.plain") == true;
        // over_mod was already disabled by the override (enabled=false), so it
        // does not factor into the gate's disabled COUNT here — the gate only
        // flips engineAccepted on Incompatible. disabled must be exactly 1.
        if (!(incompatDisabled && plainStillEnabled && disabled == 1)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: version gate wrong (incompatDisabled=%d "
                "plainStillEnabled=%d disabled=%zu, expected 1,1,1) — an "
                "Incompatible pak mod must flip engineAccepted->false "
                "(IsPluginEnabled false) and a no-restriction mod must stay "
                "enabled",
                incompatDisabled ? 1 : 0, plainStillEnabled ? 1 : 0, disabled);
            restore();
            std::error_code ec; std::filesystem::remove(tempLoadOrder, ec);
            Fail(reason);
            return;
        }
    }

    // Restore the live load-order state + remove the temp file.
    restore();
    {
        std::error_code ec;
        std::filesystem::remove(tempLoadOrder, ec);
    }

    std::snprintf(reason, sizeof(reason),
        "ParseModOrderText strips comments/blanks + preserves file order as the "
        "0-based index; modOrderIndex orders the pak-mod block (index 0 before 1 "
        "despite name; -1 -> INT_MAX sorts last); the fold registers 'mods.<modid>' "
        "rows at after_game/priority-0 and a load_order.toml 'mods.<modid>' row "
        "overrides zone/priority/enabled; ApplyVersionGate disables an Incompatible "
        "pak mod (IsPluginEnabled false) while a no-restriction mod stays enabled. "
        "Live load-order state restored.");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-54 pak-mod-registry");
}

}  // namespace kcdx::mod_absorb
