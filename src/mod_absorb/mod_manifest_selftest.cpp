#include "mod_manifest_selftest.h"

#include <cstdint>
#include <cstdio>   // snprintf (reason string)
#include <cstring>  // strcmp
#include <string>
#include <vector>

#include "mod_manifest.h"
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

// A fabricated game-version-restriction element name we DON'T yet parse, to
// exercise the defensive RE-pending WARN scan (B.2). Distinct from <version>.
constexpr const char* kManifestWithRestriction =
    "<kcd_mod><info>"
    "<name>restricted</name>"
    "<version>1.0</version>"
    "<supported_game_version>1_5</supported_game_version>"
    "</info></kcd_mod>";

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
    using version_compat::DecideGameVersionCompat;

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

    // --- Assertion 4: DecideGameVersionCompat — the shared-logic guard. ------
    // This is the regression guard for Deliverable A: a change to the extracted
    // helper that alters the decision shows up as a wrong verdict here.
    const uint32_t kRt = 0x01050489;  // a stand-in running build (nonzero).
    const std::vector<uint32_t> matchList    = {0x01040000, kRt, 0x01060000};
    const std::vector<uint32_t> nonMatchList = {0x01040000, 0x01060000};
    const std::vector<uint32_t> emptyList    = {};

    struct Case { const char* label; CompatResult got; CompatResult want; };
    const Case cases[] = {
        // (a) list containing runtimeGameVersion -> Compatible.
        {"list-contains-runtime",
         DecideGameVersionCompat(matchList, /*vi*/false, kRt),
         CompatResult::Compatible},
        // (b) non-matching list, not version-independent -> Incompatible.
        {"non-matching-not-vi",
         DecideGameVersionCompat(nonMatchList, /*vi*/false, kRt),
         CompatResult::Incompatible},
        // (c) runtimeGameVersion == 0 -> UnknownGameVersion.
        {"runtime-zero",
         DecideGameVersionCompat(nonMatchList, /*vi*/false, /*rt*/0),
         CompatResult::UnknownGameVersion},
        // (d) version_independent == true -> Compatible (even with empty list).
        {"version-independent",
         DecideGameVersionCompat(emptyList, /*vi*/true, kRt),
         CompatResult::Compatible},
        // (d') version_independent + undetected build -> still Compatible
        //      (the rule-1/rule-2 ordering ValidateManifest preserved).
        {"version-independent-undetected",
         DecideGameVersionCompat(emptyList, /*vi*/true, /*rt*/0),
         CompatResult::Compatible},
    };
    for (const Case& c : cases) {
        if (c.got != c.want) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: DecideGameVersionCompat case '%s' = %d, expected %d — the "
                "shared helper (extracted from ValidateManifest) changed the "
                "decision; this is the plugin-version regression guard",
                c.label, static_cast<int>(c.got), static_cast<int>(c.want));
            Fail(reason);
            return;
        }
    }

    // --- Assertion 5: DecideModCompat with a no-restriction manifest. --------
    // [broken: the no-restriction pak-mod gate stops returning Compatible ->
    //  every real mod (none carries a restriction) would be wrongly rejected.]
    ModManifest m;
    m.ok = true;
    m.name = kExpectName;
    const CompatResult modVerdict =
        DecideModCompat(m, /*rawXml*/xml, /*modIdForLog*/"cheat", kRt);
    if (modVerdict != CompatResult::Compatible) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: DecideModCompat on a no-restriction manifest = %d, expected "
            "Compatible(%d) — a real pak mod (none declares a restriction) would "
            "be wrongly rejected",
            static_cast<int>(modVerdict),
            static_cast<int>(CompatResult::Compatible));
        Fail(reason);
        return;
    }

    // --- Assertion 6: a restriction-looking manifest still ENABLES (Compatible)
    //     while the RE-pending WARN path is exercised (the gap is LOUD, not a
    //     silent reject). We assert only the verdict here; the one-time WARN is
    //     a log line confirmed at the feature checkpoint.
    // [broken: the defensive scan turns a candidate restriction into a reject
    //  -> a real restricted mod would be silently dropped instead of enabled +
    //  warned.]
    const std::string restrictedXml = kManifestWithRestriction;
    const CompatResult restrictedVerdict =
        DecideModCompat(m, restrictedXml, /*modIdForLog*/"restricted", kRt);
    if (restrictedVerdict != CompatResult::Compatible) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: DecideModCompat on a restriction-looking manifest = %d, "
            "expected Compatible(%d) — the RE-pending gate must ENABLE + WARN, "
            "not reject (the element name is not yet RE'd)",
            static_cast<int>(restrictedVerdict),
            static_cast<int>(CompatResult::Compatible));
        Fail(reason);
        return;
    }

    // All assertions held.
    std::snprintf(reason, sizeof(reason),
        "mod.manifest fields parse (name/description/author/version/created_on); "
        "&amp; decoded to '&'; absent <modid> -> empty; DecideGameVersionCompat "
        "verdicts correct (a/b/c/d + vi-undetected = the shared-logic guard); "
        "DecideModCompat no-restriction + restriction-looking both Compatible "
        "(RE-pending gate enables)");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-53 mod-manifest");
}

}  // namespace kcdx::mod_absorb
