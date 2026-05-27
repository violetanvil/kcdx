#include "mod_manifest_selftest.h"

#include <cstdio>   // snprintf (reason string)
#include <cstring>  // strcmp
#include <string>
#include <utility>  // std::move (assertion 4 lambda)
#include <vector>

#include "mod_manifest.h"
#include "../plugin_loader.h"   // ExtractCfgValue — the system.cfg wh_sys_version parser
#include "../test.h"
#include "../version_compat.h"

// cap-53 self-test — mod.manifest reader + the shared version-compat helper.
// Each assertion is falsifiable + names the broken state it catches (AP15).

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-53-mod-manifest-version-gate";

// A known-shape mod.manifest as a LITERAL string (NOT a file on disk). The
// <description> carries an &amp; entity so the decode path is exercised. The
// expected decoded description is kExpectDesc below.
constexpr const char* kManifestXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<kcd_mod>\n"
    "  <info>\n"
    "    <name>cheat</name>\n"
    "    <description>Tweaks &amp; cheats for KCD2</description>\n"
    "    <author>Othiden</author>\n"
    "    <version>2.21</version>\n"
    "    <created_on>Fri Mar 14 16:06:23 EDT 2025</created_on>\n"
    "    <dependencies></dependencies>\n"
    "  </info>\n"
    "</kcd_mod>\n";

constexpr const char* kExpectName    = "cheat";
constexpr const char* kExpectDesc    = "Tweaks & cheats for KCD2";  // &amp; decoded
constexpr const char* kExpectAuthor  = "Othiden";
constexpr const char* kExpectVersion = "2.21";
constexpr const char* kExpectCreated = "Fri Mar 14 16:06:23 EDT 2025";

// A manifest carrying BOTH a mod's-own <info><version> AND a <supports> block
// of TWO restriction <version> entries — the two-context discriminator. The
// <supports> parse must collect ONLY the two restriction patterns ("1.5*",
// "1.6*") and NOT the mod's own version ("3.0"), which lives under <info>.
constexpr const char* kManifestWithSupports =
    "<kcd_mod><info>"
    "<name>restricted</name>"
    "<version>3.0</version>"
    "</info>"
    "<supports>"
    "<version>1.5*</version>"
    "<version>1.6*</version>"
    "</supports>"
    "</kcd_mod>";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-53 mod-manifest");
}

}  // namespace

void RunManifestSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) {
        return;
    }
    s_reported = true;  // parsing + helper work at boot; no retry needed.

    char reason[640];
    using version_compat::CompatResult;
    using version_compat::DecideGameVersionCompatString;

    // --- Assertion 1: each field parses correctly from the literal XML. -----
    // ExtractTag is exactly the extraction ReadModManifest applies per field;
    // feeding the literal string proves the boundary/trim/decode logic without
    // a file dependency.
    // [broken: wrong tag boundary (e.g. <version> matching <version_*>),
    //  whitespace not trimmed, or content garbled -> mismatch -> FAIL.]
    const std::string xml = kManifestXml;
    struct Field { const char* tag; const char* expect; };
    const Field fields[] = {
        {"name",       kExpectName},
        {"description", kExpectDesc},
        {"author",     kExpectAuthor},
        {"version",    kExpectVersion},
        {"created_on", kExpectCreated},
    };
    for (const Field& fld : fields) {
        const std::string got = ExtractTag(xml, fld.tag);
        if (std::strcmp(got.c_str(), fld.expect) != 0) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: ExtractTag(<%s>) = \"%s\", expected \"%s\" — wrong tag "
                "boundary, untrimmed whitespace, or undecoded entity",
                fld.tag, got.c_str(), fld.expect);
            Fail(reason);
            return;
        }
    }

    // --- Assertion 2: entity decoding — &amp; in the description -> '&'. -----
    // (Covered by the field loop above since the description carries &amp;, but
    //  assert the decode in isolation so the broken-state is unambiguous.)
    // [broken: no decode -> literal "&amp;" survives -> mismatch -> FAIL.]
    const std::string decoded = DecodeEntities("a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;");
    if (std::strcmp(decoded.c_str(), "a & b <c> \"d\" 'e'") != 0) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: DecodeEntities did not decode the five entities — got \"%s\" "
            "(expected the five literals) — &amp;/&lt;/&gt;/&quot;/&apos; not handled",
            decoded.c_str());
        Fail(reason);
        return;
    }

    // --- Assertion 3: an ABSENT tag -> empty string (no crash, no garbage). --
    // [broken: missing-tag handling reads past end / returns junk -> FAIL.]
    if (!ExtractTag(xml, "modid").empty()) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: ExtractTag(<modid>) on a manifest with no <modid> returned "
            "non-empty \"%s\" (absent tag must be empty)",
            ExtractTag(xml, "modid").c_str());
        Fail(reason);
        return;
    }

    // --- Assertion 4: DecideModCompat delegates to the string gate. ----------
    // The pak-mod entry point (mod_absorb's gate) must produce the SAME verdicts
    // as DecideGameVersionCompatString for equivalent inputs — i.e. it parses
    // m.supports + forwards to the unified gate, no separate decision logic. This
    // is the pak-mod-path coverage of the unified gate (it exercises the NEW
    // DecideModCompat signature + the <supports> plumbing). Both authoring paths
    // now ride the same string gate (the earlier integer compare is removed).
    const std::string kRtStr = "1.5.1164953";  // a runtime wh_sys_version string.

    auto modCompat = [](std::vector<std::string> supports,
                        const std::string& rt) -> CompatResult {
        ModManifest mm;
        mm.ok = true;
        mm.supports = std::move(supports);
        return DecideModCompat(mm, rt);
    };

    struct Case { const char* label; CompatResult got; CompatResult want; };
    const Case cases[] = {
        // (a) a matching prefix-wildcard restriction -> Compatible.
        //     [broken: DecideModCompat doesn't forward m.supports -> wrong verdict]
        {"supports-1.5*-matches",
         modCompat({"1.5*"}, kRtStr),
         CompatResult::Compatible},
        // (b) a non-matching restriction -> Incompatible.
        //     [broken: gate ignores the restriction + blanket-Compatible (the old
        //      stub shape) -> a real restricted mod wrongly enabled -> FAIL]
        {"supports-1.6*-no-match",
         modCompat({"1.6*"}, kRtStr),
         CompatResult::Incompatible},
        // (c) <supports> absent (empty list) -> no restriction -> Compatible.
        //     [broken: empty treated as restrictive -> every real mod (none
        //      carries <supports>) wrongly rejected -> FAIL]
        {"no-supports-compatible",
         modCompat({}, kRtStr),
         CompatResult::Compatible},
        // (d) a restriction we can't evaluate (runtime string unknown) ->
        //     UnknownGameVersion (graceful-degrade, not reject).
        //     [broken: -> Incompatible (reject over our own detection failure)
        //      or crash -> FAIL]
        {"supports-vs-empty-runtime-unknown",
         modCompat({"1.5*"}, /*runtime*/""),
         CompatResult::UnknownGameVersion},
    };
    for (const Case& c : cases) {
        if (c.got != c.want) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: DecideModCompat case '%s' = %d, expected %d — the pak-mod "
                "gate does not delegate correctly to the unified string gate "
                "(m.supports not forwarded, or the verdict diverges from "
                "DecideGameVersionCompatString)",
                c.label, static_cast<int>(c.got), static_cast<int>(c.want));
            Fail(reason);
            return;
        }
    }

    // --- Assertion 5: <supports> PARSE — the two-context discriminator. -------
    // kManifestWithSupports carries an <info><version>3.0</version> (the mod's
    // OWN version) AND a <supports> block of two restriction <version> entries
    // ("1.5*", "1.6*"). ParseSupports must collect EXACTLY the two restriction
    // patterns and NOT the mod's own version.
    // [broken: the loop scanned the whole manifest instead of the <supports>
    //  block -> m.supports wrongly includes "3.0" (the <info> version) -> the
    //  count is 3 or the set contains "3.0" -> FAIL. This is the ordering-bug
    //  guard: extract the <supports> substring FIRST, then loop <version>.]
    const std::vector<std::string> parsedSupports = ParseSupports(kManifestWithSupports);
    const bool supportsExact =
        parsedSupports.size() == 2 &&
        parsedSupports[0] == "1.5*" &&
        parsedSupports[1] == "1.6*";
    if (!supportsExact) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: ParseSupports got %zu entries (expected 2: \"1.5*\",\"1.6*\") "
            "first=\"%s\" — the <supports> parse either missed an entry or "
            "leaked the <info><version> mod's-own-version (the two-context bug)",
            parsedSupports.size(),
            parsedSupports.empty() ? "" : parsedSupports[0].c_str());
        Fail(reason);
        return;
    }

    // --- Assertion 7: DecideGameVersionCompatString — the UNIFIED <supports>
    //     string-prefix-wildcard gate (sub-step 2.5a Deliverable B). Each case
    //     is falsifiable + names the broken state it catches (AP15). Reuses
    //     kRtStr from assertion 4. ---------------------------------------------
    struct StrCase { const char* label; CompatResult got; CompatResult want; };
    const StrCase strCases[] = {
        // (a) prefix wildcard hits. [broken: prefix logic wrong -> Incompatible -> FAIL]
        {"prefix-wildcard-match",
         DecideGameVersionCompatString({"1.5*"}, kRtStr),
         CompatResult::Compatible},
        // (b) prefix wildcard for a different major.minor must NOT match.
        //     [broken: wildcard over-matches -> Compatible -> FAIL]
        {"prefix-wildcard-nomatch",
         DecideGameVersionCompatString({"1.6*"}, kRtStr),
         CompatResult::Incompatible},
        // (c) exact (no '*') equal string. [broken: exact match broken -> FAIL]
        {"exact-match",
         DecideGameVersionCompatString({"1.5.1164953"}, kRtStr),
         CompatResult::Compatible},
        // (d) THE DISCRIMINATOR: no '*' is EXACT, not prefix. "1.5" must NOT
        //     match "1.5.1164953". [broken: treats no-* as prefix -> Compatible
        //     -> FAIL — this is what distinguishes wildcard from exact]
        {"no-star-is-exact-not-prefix",
         DecideGameVersionCompatString({"1.5"}, kRtStr),
         CompatResult::Incompatible},
        // (e) empty supports list = no restriction -> Compatible.
        //     [broken: empty treated as restrictive -> Incompatible/Unknown -> FAIL]
        {"empty-supports-compatible",
         DecideGameVersionCompatString({}, kRtStr),
         CompatResult::Compatible},
        // (f) non-empty list vs empty runtime string -> UnknownGameVersion.
        //     [broken: -> Incompatible (or crash) -> FAIL]
        {"nonempty-vs-empty-runtime-unknown",
         DecideGameVersionCompatString({"1.5*"}, /*runtime*/""),
         CompatResult::UnknownGameVersion},
        // (g) empty list vs empty runtime -> still Compatible (rule 1 wins over
        //     rule 2: no restriction is compatible regardless of detection).
        //     [broken: empty-runtime check ordered first -> Unknown -> FAIL]
        {"empty-list-empty-runtime-compatible",
         DecideGameVersionCompatString({}, /*runtime*/""),
         CompatResult::Compatible},
        // (h) multi-entry list, ANY matches. [broken: only-first-checked ->
        //     Incompatible -> FAIL]
        {"multi-entry-any-matches",
         DecideGameVersionCompatString({"1.4*", "1.5*"}, kRtStr),
         CompatResult::Compatible},
    };
    for (const StrCase& c : strCases) {
        if (c.got != c.want) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: DecideGameVersionCompatString case '%s' = %d, expected "
                "%d — the unified <supports> string-prefix-wildcard gate is "
                "broken (sub-step 2.5a Deliverable B)",
                c.label, static_cast<int>(c.got), static_cast<int>(c.want));
            Fail(reason);
            return;
        }
    }

    // --- Assertion 8: the system.cfg wh_sys_version parser, fed a LITERAL cfg
    //     string (no file). Exercises the case-insensitive key, whitespace
    //     around '=', quote-stripping, and absent-key -> "". -------------------
    // [broken: key match wrong, '=' split off, or quotes not stripped -> wrong
    //  value -> FAIL; absent-key returning non-empty -> FAIL]
    const std::string kCfg =
        "-- a comment line\n"
        "con_restricted = 0\n"
        "WH_SYS_Version = \"1.5.5\"\n"   // mixed case + quoted + spaces around '='
        "sys_spec=2\n";
    const std::string parsed = kcdx::plugins::ExtractCfgValue(kCfg, "wh_sys_version");
    if (std::strcmp(parsed.c_str(), "1.5.5") != 0) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: ExtractCfgValue(wh_sys_version) = \"%s\", expected \"1.5.5\" "
            "— case-insensitive key, whitespace-around-'=', or quote-stripping "
            "broke",
            parsed.c_str());
        Fail(reason);
        return;
    }
    if (!kcdx::plugins::ExtractCfgValue(kCfg, "not_present").empty()) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: ExtractCfgValue(not_present) on a cfg without that key "
            "returned non-empty (absent key must be \"\")");
        Fail(reason);
        return;
    }

    // All assertions held.
    std::snprintf(reason, sizeof(reason),
        "mod.manifest fields parse (name/description/author/version/created_on); "
        "&amp; decoded to '&'; absent <modid> -> empty; DecideModCompat delegates "
        "to the string gate (1.5* match/1.6* no-match/no-supports/unknown-runtime "
        "verdicts = pak-mod-path gate coverage); ParseSupports collects exactly "
        "the two <supports> patterns, NOT the <info> version (two-context "
        "discriminator); DecideGameVersionCompatString prefix/exact/empty/unknown/"
        "multi verdicts correct (no-*-is-exact discriminator); ExtractCfgValue "
        "parses wh_sys_version (case/quote/ws) + absent->\"\"");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-53 mod-manifest");
}

}  // namespace kcdx::mod_absorb
