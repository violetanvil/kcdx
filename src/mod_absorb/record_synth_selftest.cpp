#include "record_synth_selftest.h"

#include <cstdint>
#include <cstdio>   // snprintf (reason string)
#include <cstring>  // strcmp
#include <string>

#include "record_synth.h"
#include "../address_library.h"
#include "../test.h"

// cap-52 self-test — see record_synth_selftest.h for why this lives in engine
// code (BuildRecord is engine-internal, not a plugin export). The assertions
// below are falsifiable + each names the broken state it catches (AP15); the
// null-vtable path reports FAIL loud (not a silent skip — AP14).

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-52-mod-record-synth";

// I_Mod record field offsets — MUST mirror record_synth.cpp's field map
// (PROBE U.6.3, docs/mod-loader-absorb.md). The test reads the record BACK at
// these offsets, so a regression that moves a field in record_synth.cpp without
// moving it here makes the strcmp-equality assertion FAIL (which is the intent:
// the offsets are the contract under test).
constexpr size_t kOffVtablePrimary   = 0x00;
constexpr size_t kOffRootPathSlash   = 0x08;
constexpr size_t kOffId              = 0x10;
constexpr size_t kOffVtableSubObject = 0x18;
constexpr size_t kOffRootPathNoSlash = 0x20;
constexpr size_t kOffDisplayName     = 0x28;
constexpr size_t kOffDescription     = 0x30;
constexpr size_t kOffAuthor          = 0x38;
constexpr size_t kOffVersion         = 0x40;
constexpr size_t kOffCreatedDate     = 0x48;
constexpr size_t kTailBegin          = 0x50;
constexpr size_t kRecordSize         = 0x70;

constexpr uint64_t kImodVtablePrimaryId   = 3105;
constexpr uint64_t kImodVtableSubObjectId = 3106;

// Read a pointer-width value out of the record at `off`.
const void* ReadPtr(const void* rec, size_t off) {
    const void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(rec) + off, sizeof(v));
    return v;
}

// Read a string-field pointer (the .c_str() the module wrote) as a const char*.
const char* ReadStr(const void* rec, size_t off) {
    return static_cast<const char*>(ReadPtr(rec, off));
}

// Assert one record string field is non-null AND strcmp-equals `expected`.
// On mismatch, write a reason naming the offset + sets *failed.
bool FieldEquals(const void* rec, size_t off, const std::string& expected,
                 const char* fieldName, char* reason, size_t reasonSize) {
    const char* p = ReadStr(rec, off);
    if (p == nullptr) {
        std::snprintf(reason, reasonSize,
            "FAIL: field %s (+0x%02zx) pointer is NULL — wrong field offset, "
            "or BuildRecord did not write it",
            fieldName, off);
        return false;
    }
    if (std::strcmp(p, expected.c_str()) != 0) {
        std::snprintf(reason, reasonSize,
            "FAIL: field %s (+0x%02zx) = \"%s\", expected \"%s\" — strings "
            "freed/moved/garbled after BuildRecord returned (the LIFETIME proof)",
            fieldName, off, p, expected.c_str());
        return false;
    }
    return true;
}

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) {
        return;
    }
    s_reported = true;  // one-shot — BuildRecord works at boot, no retry needed.

    char reason[512];

    // Capture the count BEFORE this test's 2 builds. BuildRecord is a SHARED
    // engine entry — the production mod-loader takeover (and cap-57's synthetic
    // Discover) also call it, so BuiltRecordCount() is a process-wide running
    // total, NOT "records this test built". Assertion 6 checks the DELTA (this
    // test adds exactly 2), which is robust regardless of how many records the
    // takeover already synthesized. (Asserting == 2 was stale: it assumed cap-52
    // is the only BuildRecord caller, which the working takeover breaks.)
    const size_t countBefore = BuiltRecordCount();

    // --- Build record A from a known input (literal test strings). ---------
    ModRecordInput a;
    a.rootPathSlash   = "X:/test/cap52/";
    a.id              = "cap52_test_mod";
    a.rootPathNoSlash = "X:/test/cap52";
    a.displayName     = "Cap52 Test";
    a.description     = "cap-52 record-synthesis self-test record A";
    a.author          = "kcdx-suite";
    a.version         = "1.0.0";
    a.createdDate      = "2026-05-27";

    void* recA = BuildRecord(a);

    // Assertion 1: BuildRecord returns non-null.
    // [broken: vtable id 3105/3106 fails to resolve (version mismatch /
    //  unverified seed row) or the alloc is wrong → nullptr → FAIL LOUD here,
    //  not a silent skip (AP14 / the module logs the consequence too).]
    if (recA == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuildRecord(A) returned nullptr — I_Mod vtable id %llu/%llu "
            "did not resolve (version mismatch / unverified seed row), so no "
            "record could be synthesized (a null-vtable record would crash "
            "MOUNT). See the MOD_ABSORB Error line.",
            (unsigned long long)kImodVtablePrimaryId,
            (unsigned long long)kImodVtableSubObjectId);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // Assertion 2: every string field is non-null + strcmp-equals its input.
    // [broken: wrong field offset, or strings freed/moved after BuildRecord
    //  returned → garbage → FAIL — THE LIFETIME PROOF.]
    if (!FieldEquals(recA, kOffRootPathSlash,   a.rootPathSlash,   "rootPathSlash",   reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffId,              a.id,              "id",              reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffRootPathNoSlash, a.rootPathNoSlash, "rootPathNoSlash", reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffDisplayName,     a.displayName,     "displayName",     reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffDescription,     a.description,     "description",     reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffAuthor,          a.author,          "author",          reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffVersion,         a.version,         "version",         reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffCreatedDate,     a.createdDate,     "createdDate",     reason, sizeof(reason))) {
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // Assertion 3: vtables == the Address-Library-resolved targets, non-null.
    // [broken: wrong/stale/hardcoded vtable → mismatch → FAIL. Resolve()ed
    //  here too (AP1 — never hardcode the VA); same id the module uses.]
    const uintptr_t resolvedPrimary   = address_library::Resolve(kImodVtablePrimaryId);
    const uintptr_t resolvedSubObject = address_library::Resolve(kImodVtableSubObjectId);
    const void* vtPrimary   = ReadPtr(recA, kOffVtablePrimary);
    const void* vtSubObject = ReadPtr(recA, kOffVtableSubObject);
    if (resolvedPrimary == 0 || resolvedSubObject == 0 ||
        vtPrimary   != reinterpret_cast<const void*>(resolvedPrimary) ||
        vtSubObject != reinterpret_cast<const void*>(resolvedSubObject)) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: vtable mismatch — +0x00=%p vs Resolve(%llu)=%p, "
            "+0x18=%p vs Resolve(%llu)=%p (a wrong/stale/hardcoded vtable, or "
            "an unresolved id, would crash MOUNT on first virtual dispatch)",
            vtPrimary, (unsigned long long)kImodVtablePrimaryId,
            reinterpret_cast<const void*>(resolvedPrimary),
            vtSubObject, (unsigned long long)kImodVtableSubObjectId,
            reinterpret_cast<const void*>(resolvedSubObject));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // Assertion 4: the scalar tail +0x50..0x6F is all zero.
    // [broken: un-zeroed tail → MOUNT reads a garbage flag → FAIL.]
    for (size_t i = kTailBegin; i < kRecordSize; ++i) {
        const uint8_t b = static_cast<const uint8_t*>(recA)[i];
        if (b != 0) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: record tail not zeroed — byte +0x%02zx = 0x%02x (the "
                "scalar/flag tail must be all zero; a garbage flag misleads MOUNT)",
                i, b);
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
            return;
        }
    }

    // --- Build a SECOND record B with DIFFERENT literal strings. -----------
    ModRecordInput b;
    b.rootPathSlash   = "Y:/other/cap52b/";
    b.id              = "cap52_other_mod";
    b.rootPathNoSlash = "Y:/other/cap52b";
    b.displayName     = "Cap52 Other";
    b.description     = "cap-52 record-synthesis self-test record B (different)";
    b.author          = "kcdx-suite-b";
    b.version         = "2.5.1";
    b.createdDate      = "2026-05-28";

    void* recB = BuildRecord(b);
    if (recB == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuildRecord(B) returned nullptr on the SECOND build "
            "(vtable id %llu/%llu stopped resolving between builds)",
            (unsigned long long)kImodVtablePrimaryId,
            (unsigned long long)kImodVtableSubObjectId);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // Assertion 5 (THE KEY CHECK): RE-READ record A's string fields after B
    // was built; they MUST still strcmp-equal A's ORIGINAL inputs.
    // [broken: if the module interned strings in a reallocating container
    //  (std::vector<std::string>), B's push_back would relocate A's stored
    //  strings, dangling the .c_str() pointers already written into recA →
    //  recA now reads garbage → FAIL. THE CONTAINER-STABILITY PROOF.]
    if (!FieldEquals(recA, kOffRootPathSlash,   a.rootPathSlash,   "rootPathSlash(after-B)",   reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffId,              a.id,              "id(after-B)",              reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffRootPathNoSlash, a.rootPathNoSlash, "rootPathNoSlash(after-B)", reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffDisplayName,     a.displayName,     "displayName(after-B)",     reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffDescription,     a.description,     "description(after-B)",     reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffAuthor,          a.author,          "author(after-B)",          reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffVersion,         a.version,         "version(after-B)",         reason, sizeof(reason)) ||
        !FieldEquals(recA, kOffCreatedDate,     a.createdDate,     "createdDate(after-B)",     reason, sizeof(reason))) {
        // FieldEquals already wrote the offending field into reason; prepend
        // the container-stability context so the failure is unambiguous.
        char full[640];
        std::snprintf(full, sizeof(full),
            "CONTAINER-STABILITY FAIL: record A's field dangled after record B "
            "was built — %s. (A reallocating string container would relocate "
            "A's interned strings on B's build; the deque is the growth-stable "
            "choice.)", reason);
        kcdx::test::ReportResult(kRow, false, full);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // Assertion 6: BuiltRecordCount() grew by exactly 2 across this test's 2
    // builds (DELTA, not absolute — the count is a shared process-wide total).
    // [broken: ownership container not appending each synthesized record → the
    //  delta is < 2 → FAIL.]
    const size_t countAfter = BuiltRecordCount();
    const size_t delta = countAfter - countBefore;
    if (delta != 2) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuiltRecordCount() grew by %zu across this test's 2 builds "
            "(expected 2; before=%zu after=%zu) — the ownership container is not "
            "appending each synthesized record",
            delta, countBefore, countAfter);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
        return;
    }

    // All assertions held.
    std::snprintf(reason, sizeof(reason),
        "2 records built (BuiltRecordCount delta=2); A's 8 string fields intact "
        "after B built (container-stability proof); vtables = Resolve(%llu)/Resolve(%llu) "
        "non-null; tail +0x50..0x6F zero",
        (unsigned long long)kImodVtablePrimaryId,
        (unsigned long long)kImodVtableSubObjectId);
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-52 record-synth");
}

}  // namespace kcdx::mod_absorb
