#include "order_persist_selftest.h"

#include <cstdio>   // snprintf
#include <string>
#include <unordered_map>
#include <vector>

#include "order_persist.h"
#include "pak_mod_registry.h"  // ParseModOrderText (the mod_order.txt round-trip)
#include "../test.h"

// cap-56 self-test — order persistence. Each assertion is falsifiable + names
// the broken state it catches (a non-falsifiable PASS proves nothing). Every
// assertion is PURE (literal string in, string out) — no global state, no file
// I/O — so there is nothing to snapshot/restore.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-56-order-persist";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-56 order-persist");
}

order_persist::ResolvedRow PakRow(const std::string& modId,
                                  const std::string& human) {
    order_persist::ResolvedRow r;
    r.loadOrderName = "mods." + modId;
    r.humanName     = human;
    r.isPakMod      = true;
    return r;
}

order_persist::ResolvedRow PluginRow(const std::string& name) {
    order_persist::ResolvedRow r;
    r.loadOrderName = name;
    r.isPakMod      = false;
    return r;
}

bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

void RunOrderPersistSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;

    char reason[1024];

    // ========================================================================
    // Assertion 1 (UNIT, pure): load_order.toml row serialization. Merging a
    // resolved set (one pak mod, one plugin) into an EMPTY base produces a
    // "[[plugin]]" table for each, the pak-mod row keyed "mods.<modid>" with the
    // human name as a trailing '#' comment, the plugin row keyed by its bare
    // name with NO comment.
    // [broken: row missing -> name not in output -> FAIL; wrong key (bare modid
    //  not "mods."-prefixed) -> FAIL; human name absent -> FAIL]
    // ========================================================================
    {
        const std::vector<order_persist::ResolvedRow> rows = {
            PakRow("inventory_in_dialogue", "Inventory In Dialogue + Quicksave"),
            PluginRow("my_plugin"),
        };
        std::vector<std::string> added;
        const std::string out =
            order_persist::MergeLoadOrderToml("", rows, &added);

        const bool ok =
            Contains(out, "name    = \"mods.inventory_in_dialogue\"") &&
            Contains(out, "# Inventory In Dialogue + Quicksave") &&
            Contains(out, "name    = \"my_plugin\"") &&
            added.size() == 2;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: load_order.toml row serialization — out did not contain "
                "the expected 'mods.<modid>' row, the human-name comment, or the "
                "plugin row (added=%zu, expected 2). The pak-mod row must be "
                "keyed \"mods.<modid>\" and carry the human name as a trailing "
                "'#' comment (Read() rejects a display_name KEY, so the name can "
                "only ride as a comment). OUTPUT:\n%s",
                added.size(), out.c_str());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 2 (UNIT, pure): idempotence. Merging the SAME rows into the
    // OUTPUT of merge-1 adds NOTHING (every name now has a row) -> byte-
    // identical text. This is the write-if-changed guarantee: a steady-state
    // boot produces identical bytes, so nothing is rewritten.
    // [broken: a non-idempotent merge (re-appends a row that already exists,
    //  reorders, or reformats) -> out2 != out1 -> FAIL]
    // ========================================================================
    {
        const std::vector<order_persist::ResolvedRow> rows = {
            PakRow("alpha", "Alpha Mod"),
            PluginRow("beta_plugin"),
        };
        const std::string out1 = order_persist::MergeLoadOrderToml("", rows);
        std::vector<std::string> added2;
        const std::string out2 =
            order_persist::MergeLoadOrderToml(out1, rows, &added2);
        if (out1 != out2 || !added2.empty()) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: merge is NOT idempotent — merging the same rows into "
                "merge-1's output added %zu row(s) (expected 0) and/or changed "
                "the bytes. A second merge must add nothing (every name already "
                "has a row), so a steady-state boot writes identical bytes.\n"
                "OUT1:\n%s\nOUT2:\n%s",
                added2.size(), out1.c_str(), out2.c_str());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 3 (UNIT, pure): mod_order.txt serialization + round-trip.
    // SerializeModOrderText emits one bare modid per line in the given order
    // (after a '#' comment header); ParseModOrderText round-trips it back to
    // the SAME sequence with the comment stripped.
    // [broken: order lost / reordered -> wrong index -> FAIL; modid mangled ->
    //  missing key -> FAIL; comment not stripped on round-trip -> a phantom
    //  entry -> FAIL]
    // ========================================================================
    {
        const std::vector<std::string> order = {"zeta", "alpha", "gamma"};
        const std::string text = order_persist::SerializeModOrderText(order);
        const std::unordered_map<std::string, int> parsed =
            ParseModOrderText(text);
        const bool ok =
            parsed.size() == 3 &&
            parsed.count("zeta")  && parsed.at("zeta")  == 0 &&
            parsed.count("alpha") && parsed.at("alpha") == 1 &&
            parsed.count("gamma") && parsed.at("gamma") == 2;
        // Also: the write-if-changed compare sees NO diff between the serialized
        // text and the same order (the comment header is ignored in the diff).
        const bool noDiff = !order_persist::ModOrderDiffers(text, order);
        if (!ok || !noDiff) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: mod_order.txt round-trip — parsed %zu entries (expected "
                "zeta=0,alpha=1,gamma=2), ModOrderDiffers(serialized, order)=%d "
                "(expected 0). The serializer must emit one bare modid per line "
                "in order; the parser must strip the comment header and recover "
                "the exact sequence (so write-if-changed sees no diff).\nTEXT:\n%s",
                parsed.size(), order_persist::ModOrderDiffers(text, order) ? 1 : 0,
                text.c_str());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 4 (UNIT, pure): merge PRESERVES existing rows. Given an existing
    // load_order.toml (a hand-edited plugin row with a user comment + an
    // existing pak-mod row) + a resolved set that re-lists those two AND adds a
    // newly-discovered mod, the existing text survives VERBATIM (every byte of
    // the base is a prefix of the output) and ONLY the new mod's row is added.
    // [broken: an existing row is rewritten/reformatted -> base not a prefix ->
    //  FAIL; the user's hand-edit comment is lost -> FAIL; a re-listed existing
    //  mod is duplicated -> added>1 -> FAIL]
    // ========================================================================
    {
        const std::string existing =
            "# my hand-edited file\n"
            "[[plugin]]\n"
            "name = \"my_plugin\"\n"
            "enabled = false   # I disabled this on purpose\n"
            "\n"
            "[[plugin]]\n"
            "name = \"mods.existing_mod\"  # Existing Mod\n"
            "priority = 10\n";
        const std::vector<order_persist::ResolvedRow> rows = {
            PluginRow("my_plugin"),                       // already present
            PakRow("existing_mod", "Existing Mod"),       // already present
            PakRow("new_mod", "Brand New Mod"),           // NEWLY discovered
        };
        std::vector<std::string> added;
        const std::string out =
            order_persist::MergeLoadOrderToml(existing, rows, &added);

        const bool basePreservedVerbatim = out.rfind(existing, 0) == 0;  // prefix
        const bool handEditKept =
            Contains(out, "enabled = false   # I disabled this on purpose");
        const bool onlyNewAdded =
            added.size() == 1 && added[0] == "mods.new_mod";
        const bool newRowPresent =
            Contains(out, "name    = \"mods.new_mod\"") &&
            Contains(out, "# Brand New Mod");
        if (!(basePreservedVerbatim && handEditKept && onlyNewAdded &&
              newRowPresent)) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: merge did not preserve existing rows — basePrefix=%d "
                "handEditKept=%d onlyNewAdded=%d(added=%zu) newRowPresent=%d. "
                "Existing rows (plugin + pak-mod, including hand-edits) MUST "
                "survive verbatim; ONLY a newly-discovered mod's row is appended "
                "(add-only — kcdx never overwrites a row the user owns).\n"
                "OUTPUT:\n%s",
                basePreservedVerbatim ? 1 : 0, handEditKept ? 1 : 0,
                onlyNewAdded ? 1 : 0, added.size(), newRowPresent ? 1 : 0,
                out.c_str());
            Fail(reason);
            return;
        }
    }

    std::snprintf(reason, sizeof(reason),
        "MergeLoadOrderToml adds a 'mods.<modid>' [[plugin]] row carrying the "
        "human mod name as a trailing '#' comment (+ a bare plugin row); the "
        "merge is byte-idempotent (a second merge of the same rows adds nothing); "
        "SerializeModOrderText emits one bare modid per line in resolved order "
        "and ParseModOrderText round-trips it (comments stripped, no diff); and "
        "the merge preserves existing rows VERBATIM (hand-edits kept, only a "
        "newly-discovered mod appended). All assertions pure — no global state "
        "touched. (Live on-disk write + write-if-changed skip + fail-loud are "
        "the verification checkpoint.)");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-56 order-persist");
}

}  // namespace kcdx::mod_absorb
