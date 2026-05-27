#include "record_validate_selftest.h"

#include <cstdint>
#include <cstdio>   // snprintf
#include <cstring>  // memcpy
#include <string>

#include "record_synth.h"
#include "record_validate.h"
#include "../test.h"

// cap-58 self-test — see record_validate_selftest.h for why this lives in
// engine code. Each assertion is FALSIFIABLE + names the broken state it
// catches. The reject cases are the load-bearing proof: record_synth builds
// correct records, so the accept case alone would pass even if the validator
// were a no-op — the deliberately-malformed records are what prove it guards.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kRow = "cap-58-record-validate";

// Field offsets — MUST mirror record_synth.cpp's field map.
constexpr size_t kOffVtablePrimary = 0x00;
constexpr size_t kOffDisplayName   = 0x28;

// CryString nLength lives at data-8 (the load-bearing invariant the engine
// reads to size string copies).
constexpr size_t kCryStrNLengthBackOff = 8;

// A known-good input for BuildRecord — non-empty fields so each CryString has a
// real length to corrupt / validate.
ModRecordInput MakeGoodInput(const std::string& tag) {
    ModRecordInput in;
    in.rootPathSlash   = "Z:\\test\\cap58\\" + tag + "/";
    in.id              = "cap58_" + tag;
    in.rootPathNoSlash = "Z:\\test\\cap58\\" + tag;
    in.displayName     = "Cap58 " + tag;
    in.description     = "cap-58 validator self-test record (" + tag + ")";
    in.author          = "kcdx-suite";
    in.version         = "1.0.0";
    in.createdDate     = "2026-05-27";
    return in;
}

// Read a pointer-width value out of a record at `off`.
void* ReadPtr(const void* rec, size_t off) {
    void* v = nullptr;
    std::memcpy(&v, static_cast<const uint8_t*>(rec) + off, sizeof(v));
    return v;
}

void Report(bool pass, const char* reason) {
    kcdx::test::ReportResult(kRow, pass, reason);
    kcdx::test::EmitSummaryIfChanged("cap-58 record-validate");
}

}  // namespace

void RunRecordValidateSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;  // one-shot — BuildRecord + the validator work at boot.

    char reason[768];

    // ========================================================================
    // Assertion 1 (ACCEPT): a WELL-FORMED record built by record_synth must
    // VALIDATE. [broken: the validator is too strict / rejects a correct
    // record -> false here -> FAIL — the guard would drop every real mod]
    // ========================================================================
    void* good = BuildRecord(MakeGoodInput("good"));
    if (good == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuildRecord(good) returned nullptr — I_Mod vtable id 3105/3106 "
            "did not resolve (version mismatch / unverified seed row); cannot run "
            "the validator self-test. See the MOD_ABSORB Error line.");
        Report(false, reason);
        return;
    }
    if (!ValidateSynthRecord(good, "cap58.good", "cap58_good")) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: ValidateSynthRecord REJECTED a well-formed record built by "
            "record_synth::BuildRecord — the validator is too strict and would "
            "drop every real mod from the enabled list. See the MOD_ABSORB Error "
            "line for which field/invariant it wrongly failed.");
        Report(false, reason);
        return;
    }

    // ========================================================================
    // Assertion 2 (REJECT — corrupted CryString nLength): build a fresh record,
    // then corrupt the displayName field's nLength header word (data-8) so it no
    // longer equals strlen — EXACTLY the garbage-length state that crashed MOUNT.
    // The validator MUST reject it. [broken: the validator does not read/compare
    // nLength (a no-op guard) -> returns true on the corrupted record -> FAIL —
    // THE LOAD-BEARING PROOF the guard actually checks the length invariant]
    // ========================================================================
    void* badLen = BuildRecord(MakeGoodInput("badlen"));
    if (badLen == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuildRecord(badlen) returned nullptr on the second build "
            "(vtable id 3105/3106 stopped resolving between builds)");
        Report(false, reason);
        return;
    }
    {
        // displayName field -> char data -> nLength at data-8. Overwrite it with
        // a deliberately-wrong value (strlen + 9999), != strlen, mimicking the
        // garbage-header bug that drives the fatal multi-GB allocation.
        char* data = static_cast<char*>(ReadPtr(badLen, kOffDisplayName));
        int32_t corruptLen = static_cast<int32_t>(std::strlen(data)) + 9999;
        std::memcpy(data - kCryStrNLengthBackOff, &corruptLen, sizeof(corruptLen));

        if (ValidateSynthRecord(badLen, "cap58.badlen", "cap58_badlen")) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: ValidateSynthRecord ACCEPTED a record whose displayName "
                "CryString nLength (data-8) was corrupted to strlen+9999 != strlen "
                "— the validator is not enforcing the load-bearing nLength==strlen "
                "invariant, so the garbage-length MOUNT crash is NOT guarded. The "
                "guard does nothing.");
            Report(false, reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 3 (REJECT — null vtable): build a fresh record, then null its
    // +0x00 primary vtable slot. The validator MUST reject it. [broken: the
    // validator does not check the vtable slots -> returns true on the
    // null-vtable record -> FAIL — a null vtable crashes MOUNT on the first
    // virtual dispatch and would go uncaught]
    // ========================================================================
    void* badVt = BuildRecord(MakeGoodInput("badvt"));
    if (badVt == nullptr) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: BuildRecord(badvt) returned nullptr on the third build "
            "(vtable id 3105/3106 stopped resolving between builds)");
        Report(false, reason);
        return;
    }
    {
        void* nullVt = nullptr;
        std::memcpy(static_cast<uint8_t*>(badVt) + kOffVtablePrimary,
                    &nullVt, sizeof(nullVt));

        if (ValidateSynthRecord(badVt, "cap58.badvt", "cap58_badvt")) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: ValidateSynthRecord ACCEPTED a record whose +0x00 primary "
                "vtable slot was nulled — the validator is not checking the vtable "
                "slots, so a null/out-of-range vtable (which crashes MOUNT on the "
                "first virtual dispatch) would go uncaught.");
            Report(false, reason);
            return;
        }
    }

    // All assertions held: accepts a well-formed record; rejects a corrupted
    // nLength header (the keystone-crash invariant); rejects a null vtable.
    std::snprintf(reason, sizeof(reason),
        "validator ACCEPTS a well-formed record (record_synth::BuildRecord); "
        "REJECTS a corrupted CryString nLength (data-8 != strlen — the keystone "
        "garbage-length crash); REJECTS a nulled +0x00 vtable. The reject cases "
        "prove the guard checks the invariants, not a no-op pass.");
    Report(true, reason);
}

}  // namespace kcdx::mod_absorb
