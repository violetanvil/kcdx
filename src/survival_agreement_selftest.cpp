#include "survival_agreement_selftest.h"

#include <windows.h>  // IMAGE_* section/characteristic constants

#include <cstdint>
#include <cstdio>   // snprintf
#include <cstdlib>  // strtod
#include <cstring>  // memcpy/memcmp/strcmp/strncmp
#include <string>
#include <utility>  // std::pair, std::move
#include <vector>

#include "log.h"
#include "patch_engine.h"             // patch::ParsePattern (decode the stored AOB → bytes+mask)
#include "survival.h"                 // SurvivalCheckOnBuffer (the buffer-injection seam)
#include "survival_agreement_fixture.h"  // kFixtureJson (GENERATED — the embedded fixture)
#include "test.h"

// cap-85 — see survival_agreement_selftest.h for the full contract + why this
// lives in engine code.

namespace kcdx::survival_agreement_selftest {

namespace {

constexpr const char* kRow = "cap-85-survival-agreement";
constexpr const char* kCategory = "SURVAGREE";

namespace sv = kcdx::survival;

// ===========================================================================
// A COMPACT, DEPENDENCY-FREE JSON READER — scoped to the fixture's machine-
// generated shape (objects / arrays / strings / numbers / bools, standard
// escapes). The engine vendors no JSON library and the fixture is the contract
// the C++ side reads, so a small recursive-descent reader is the test-harness
// tool (working-artifacts.md — test code, not production). The input is emitted
// by Python's json.dump (well-formed, predictable); the reader FAILS LOUD on
// anything unexpected (no silent partial parse — a malformed contract is a hard
// error, never a faked verdict).
// ===========================================================================
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool                                           boolVal = false;
    double                                         numVal = 0.0;
    std::string                                    strVal;
    std::vector<JsonValue>                         arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* find(const std::string& key) const {
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
    bool      isNumber() const { return type == Type::Number; }
    bool      isString() const { return type == Type::String; }
    long long asInt() const { return static_cast<long long>(numVal); }
};

class JsonParser {
public:
    explicit JsonParser(const char* s) : p_(s) {}

    JsonValue parse(bool& ok) {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        ok = !failed_;
        return v;
    }
    const std::string& error() const { return err_; }

private:
    const char* p_;
    bool        failed_ = false;
    std::string err_;

    void fail(const char* why) { if (!failed_) { failed_ = true; err_ = why; } }
    void skipWs() { while (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r') ++p_; }

    JsonValue parseValue() {
        if (failed_) return JsonValue{};
        skipWs();
        switch (*p_) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { JsonValue v; v.type = JsonValue::Type::String; v.strVal = parseString(); return v; }
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }

    JsonValue parseObject() {
        JsonValue v; v.type = JsonValue::Type::Object;
        ++p_;  // '{'
        skipWs();
        if (*p_ == '}') { ++p_; return v; }
        for (;;) {
            skipWs();
            if (*p_ != '"') { fail("object key not a string"); return v; }
            std::string key = parseString();
            skipWs();
            if (*p_ != ':') { fail("missing ':' in object"); return v; }
            ++p_;
            JsonValue val = parseValue();
            v.obj.emplace_back(std::move(key), std::move(val));
            skipWs();
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == '}') { ++p_; break; }
            fail("malformed object (expected ',' or '}')");
            return v;
        }
        return v;
    }

    JsonValue parseArray() {
        JsonValue v; v.type = JsonValue::Type::Array;
        ++p_;  // '['
        skipWs();
        if (*p_ == ']') { ++p_; return v; }
        for (;;) {
            JsonValue el = parseValue();
            v.arr.push_back(std::move(el));
            skipWs();
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == ']') { ++p_; break; }
            fail("malformed array (expected ',' or ']')");
            return v;
        }
        return v;
    }

    std::string parseString() {
        std::string out;
        ++p_;  // opening quote
        while (*p_ && *p_ != '"') {
            char c = *p_++;
            if (c == '\\') {
                char e = *p_++;
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = *p_++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else { fail("bad \\u escape"); return out; }
                        }
                        out.push_back(static_cast<char>(cp & 0xFF));  // fixture text is ASCII.
                        break;
                    }
                    default: fail("bad string escape"); return out;
                }
            } else {
                out.push_back(c);
            }
        }
        if (*p_ != '"') { fail("unterminated string"); return out; }
        ++p_;  // closing quote
        return out;
    }

    JsonValue parseNumber() {
        JsonValue v; v.type = JsonValue::Type::Number;
        char* end = nullptr;
        v.numVal = std::strtod(p_, &end);
        if (end == p_) { fail("bad number"); return v; }
        p_ = end;
        return v;
    }

    JsonValue parseBool() {
        JsonValue v; v.type = JsonValue::Type::Bool;
        if (std::strncmp(p_, "true", 4) == 0) { v.boolVal = true; p_ += 4; }
        else if (std::strncmp(p_, "false", 5) == 0) { v.boolVal = false; p_ += 5; }
        else fail("bad bool");
        return v;
    }

    JsonValue parseNull() {
        JsonValue v; v.type = JsonValue::Type::Null;
        if (std::strncmp(p_, "null", 4) == 0) p_ += 4; else fail("bad null");
        return v;
    }
};

// Decode a hex string ("48895c..") into bytes. False on odd-length / non-hex.
bool HexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    auto nib = [](char c, int& v) -> bool {
        if (c >= '0' && c <= '9') { v = c - '0'; return true; }
        if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
        if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
        return false;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = 0, lo = 0;
        if (!nib(hex[i], hi) || !nib(hex[i + 1], lo)) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

// ===========================================================================
// THE SYNTHETIC-PE BUILDER — the C++ analogue of the JS makeFakePE/makeAnchorPE.
// The engine's static checks scan PE SECTIONS (pe::OnDisk* accessors parse a full
// PE buffer's headers + sections); the fixture slices are JUST the kind's bytes
// (no PE scaffolding). So the agreement test PLANTS each slice's bytes in a
// minimal on-disk PE at the right section + rva, exactly as the engine's check
// reads them. A test-harness helper (not production) — it wraps raw bytes in a
// PE the OnDisk* accessors parse. Layout mirrors makeFakePE.ts's documented bytes
// (PE32+, ImageBase 0x180000000).
// ===========================================================================
constexpr uint64_t kImageBase   = 0x180000000ULL;  // == makeFakePE.ts IMAGE_BASE.
constexpr uint32_t kPeOffset    = 0x80;             // e_lfanew → PE signature.
constexpr uint16_t kOptHdrSize  = 0xF0;             // PE32+ optional header size (240).
constexpr uint32_t kSecTableOff =
    kPeOffset + 4 + static_cast<uint32_t>(sizeof(IMAGE_FILE_HEADER)) + kOptHdrSize;

struct PeSectionSpec {
    std::string          name;             // ".text" / ".rdata" / ".data"
    uint32_t             rva = 0;          // section VirtualAddress
    uint32_t             virtualSize = 0;  // Misc.VirtualSize (in-memory extent)
    uint32_t             characteristics = 0;
    std::vector<uint8_t> raw;             // the section's on-disk raw data
};

void Put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void Put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}
void Put64(std::vector<uint8_t>& b, size_t off, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

// Build a synthetic PE32+ holding the given sections, each at a 0x200-aligned file
// offset after the section table. A section's declared VirtualSize is the larger
// of its raw size and its requested `virtualSize` (so a section can present a
// large in-memory extent — the vtable_base .text window — with a tiny raw block;
// IsRvaInExecutableSection uses max(VirtualSize, SizeOfRawData)). Returns the
// on-disk PE buffer the pe::OnDisk* accessors parse.
std::vector<uint8_t> BuildSyntheticPe(const std::vector<PeSectionSpec>& sections) {
    const uint16_t n = static_cast<uint16_t>(sections.size());
    std::vector<uint32_t> fileOffs(n);
    uint32_t cursor = 0x400;  // first section raw data (PE-typical).
    for (uint16_t i = 0; i < n; ++i) {
        fileOffs[i] = cursor;
        uint32_t rawSize = static_cast<uint32_t>(sections[i].raw.size());
        uint32_t aligned = (rawSize + 0x1FF) & ~0x1FFu;
        if (aligned == 0) aligned = 0x200;
        cursor += aligned;
    }
    std::vector<uint8_t> buf(cursor, 0);

    // DOS header.
    Put16(buf, 0, IMAGE_DOS_SIGNATURE);   // "MZ"
    Put32(buf, 0x3C, kPeOffset);          // e_lfanew

    // PE signature.
    Put32(buf, kPeOffset, IMAGE_NT_SIGNATURE);  // "PE\0\0"

    // COFF file header.
    const uint32_t coff = kPeOffset + 4;
    Put16(buf, coff + 0, IMAGE_FILE_MACHINE_AMD64);
    Put16(buf, coff + 2, n);
    Put16(buf, coff + 16, kOptHdrSize);
    Put16(buf, coff + 18, IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL);

    // PE32+ optional header (Magic + ImageBase; the rest zero is fine — the
    // OnDisk* accessors read only the section table + ImageBase).
    const uint32_t opt = coff + static_cast<uint32_t>(sizeof(IMAGE_FILE_HEADER));
    Put16(buf, opt + 0, IMAGE_NT_OPTIONAL_HDR64_MAGIC);  // PE32+
    Put64(buf, opt + 24, kImageBase);                    // ImageBase (u64 @ +24)

    // Section table — one 40-byte entry per section.
    for (uint16_t i = 0; i < n; ++i) {
        const PeSectionSpec& s = sections[i];
        const uint32_t e = kSecTableOff + i * 40;
        for (size_t k = 0; k < 8 && k < s.name.size(); ++k) buf[e + k] = static_cast<uint8_t>(s.name[k]);
        const uint32_t rawSize = static_cast<uint32_t>(s.raw.size());
        uint32_t vsize = s.virtualSize > rawSize ? s.virtualSize : rawSize;
        Put32(buf, e + 8, vsize);            // Misc.VirtualSize
        Put32(buf, e + 12, s.rva);           // VirtualAddress
        Put32(buf, e + 16, rawSize);         // SizeOfRawData
        Put32(buf, e + 20, fileOffs[i]);     // PointerToRawData
        Put32(buf, e + 36, s.characteristics);
        if (rawSize > 0) std::memcpy(buf.data() + fileOffs[i], s.raw.data(), rawSize);
    }
    return buf;
}

// Section characteristic presets matching the engine's OnDisk* predicates:
//   .text  — read + execute        (OnDiskExecutableSections)
//   .data  — read + write, not exec (OnDiskWritableDataSections)
constexpr uint32_t kTextChars = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
constexpr uint32_t kDataChars =
    IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA;

const char* VerdictForStatus(sv::Status s) {
    switch (s) {
        case sv::Status::Unchanged:   return "Unchanged";
        case sv::Status::Changed:     return "Changed";
        case sv::Status::Ambiguous:   return "Ambiguous";
        case sv::Status::CannotCheck: return "CannotCheck";
    }
    return "?";
}

// The accumulated pin outcome — the first divergence (if any) names a concrete
// kind/slice so the report is actionable.
struct AgreementOutcome {
    bool        diverged = false;
    int         checked = 0;
    std::string divergeMsg;
};

// Run the engine static check over the planted PE + compare to the fixture's
// pinned verdict. The FIRST divergence wins (names a concrete kind/slice). NEVER
// weakens the comparison — a mismatch is a real divergence the conformance test
// caught (AP15), surfaced as a FAIL, never masked.
void AssertSlice(AgreementOutcome& out,
                 const char* kind, const std::string& rowName,
                 const std::string& sliceName, const std::string& pinned,
                 const sv::Payload& payload, uint32_t rva,
                 const std::vector<uint8_t>& planted) {
    sv::Result r = sv::SurvivalCheckOnBuffer(payload, rva, planted);
    const char* engineVerdict = VerdictForStatus(r.status);
    ++out.checked;
    if (pinned != engineVerdict) {
        if (!out.diverged) {
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "kind=%s row=%s slice=%s: engine verdict '%s' (reason='%s') != fixture pinned '%s'",
                kind, rowName.c_str(), sliceName.c_str(),
                engineVerdict, r.reason.c_str(), pinned.c_str());
            out.divergeMsg = buf;
        }
        out.diverged = true;
        LOG_WARN_KV(kCategory, "agreement_diverge",
            ::kcdx::log::KV("kind", kind),
            ::kcdx::log::KV("row", rowName.c_str()),
            ::kcdx::log::KV("slice", sliceName.c_str()),
            ::kcdx::log::KV("engine_verdict", engineVerdict),
            ::kcdx::log::KV("engine_reason", r.reason.empty() ? "-" : r.reason.c_str()),
            ::kcdx::log::KV("fixture_pinned", pinned.c_str()));
    } else {
        LOG_DEBUG_KV(kCategory, "agreement_ok",
            ::kcdx::log::KV("kind", kind),
            ::kcdx::log::KV("slice", sliceName.c_str()),
            ::kcdx::log::KV("verdict", engineVerdict));
    }
}

constexpr uint32_t kPlantRva = 0x1000;  // the section RVA a slice is planted at.

// --- function: body hash over a planted readable span. ----------------------
void PlantFunction(AgreementOutcome& out, const std::string& rowName,
                   const std::string& sliceName, const std::string& pinned,
                   const std::vector<uint8_t>& body,
                   const std::vector<uint8_t>& contentHash, uint64_t length) {
    std::vector<uint8_t> raw = body;
    if (raw.size() < length) raw.resize(static_cast<size_t>(length), 0x00);
    PeSectionSpec text{".text", kPlantRva, 0, kTextChars, raw};
    std::vector<uint8_t> pe = BuildSyntheticPe({text});

    sv::Payload p;
    p.kind = sv::Kind::Function;
    p.contentHash = contentHash;
    p.length = static_cast<size_t>(length);
    AssertSlice(out, "function", rowName, sliceName, pinned, p, kPlantRva, pe);
}

// --- callsite: AOB re-match over a planted .text block. ---------------------
void PlantCallsite(AgreementOutcome& out, const std::string& rowName,
                   const std::string& sliceName, const std::string& pinned,
                   const std::vector<uint8_t>& block, const sv::Payload& aobPayload) {
    PeSectionSpec text{".text", kPlantRva, 0, kTextChars, block};
    std::vector<uint8_t> pe = BuildSyntheticPe({text});
    sv::Payload p = aobPayload;
    p.kind = sv::Kind::Callsite;
    AssertSlice(out, "callsite", rowName, sliceName, pinned, p, kPlantRva, pe);
}

// --- vtable_base: N-qword table-shape over a planted .data table. -----------
// The table sits in .data; each qword holds a .text RVA. The engine's
// IsTextPointerOnDisk reads a qword as an absolute (ImageBase + rva), so rebase
// the fixture's stored RVAs to absolutes; it then range-tests the RVA against the
// executable sections, so the PE declares a .text section spanning the fixture's
// [textLo, textHi) window (a large VirtualSize, tiny raw). A 0 slot stays 0 (the
// fixture's "changed" non-pointer slot reads as not-a-.text-pointer → Changed).
void PlantVtableBase(AgreementOutcome& out, const std::string& rowName,
                     const std::string& sliceName, const std::string& pinned,
                     const std::vector<uint8_t>& table, uint32_t slotCount,
                     uint32_t textLo, uint32_t textHi) {
    std::vector<uint8_t> rebased = table;
    for (size_t i = 0; i + 8 <= rebased.size(); i += 8) {
        uint64_t v = 0;
        std::memcpy(&v, rebased.data() + i, 8);
        if (v != 0) {
            uint64_t abs = kImageBase + v;
            std::memcpy(rebased.data() + i, &abs, 8);
        }
    }
    const uint32_t textRva = textLo == 0 ? 0x1000 : textLo;
    const uint32_t textWindow = (textHi > textRva) ? (textHi - textRva) : 0x1000;
    const uint32_t dataRva = textHi + 0x1000;

    PeSectionSpec text;
    text.name = ".text";
    text.rva = textRva;
    text.virtualSize = textWindow;            // large in-memory extent, tiny raw.
    text.characteristics = kTextChars;
    text.raw = std::vector<uint8_t>(0x10, 0x90);  // a tiny on-disk stub (NOPs).

    PeSectionSpec data{".data", dataRva, 0, kDataChars, rebased};

    std::vector<uint8_t> pe = BuildSyntheticPe({text, data});

    sv::Payload p;
    p.kind = sv::Kind::VtableBase;
    p.slotCount = slotCount;
    AssertSlice(out, "vtable_base", rowName, sliceName, pinned, p, dataRva, pe);
}

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;  // deterministic + synthetic — no retry needed.

    char reason[1024];
    AgreementOutcome out;

    // --- Parse the embedded fixture JSON (the cross-language contract). --------
    JsonParser parser(kcdx::survival_agreement_fixture::kFixtureJson);
    bool ok = false;
    JsonValue root = parser.parse(ok);
    if (!ok || root.type != JsonValue::Type::Object) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: could not parse the embedded cross-impl fixture JSON (%s) — the "
            "GENERATED survival_agreement_fixture.h is malformed; regenerate it from "
            "cross_impl_fixture.py.", parser.error().c_str());
        LOG_ERROR_KV(kCategory, "selftest_fail", ::kcdx::log::KV("subcheck", "parse"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
        return;
    }

    const JsonValue* rows = root.find("rows");
    if (rows == nullptr || rows->type != JsonValue::Type::Array || rows->arr.empty()) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the embedded fixture JSON has no 'rows' array — the contract is "
            "empty; regenerate survival_agreement_fixture.h.");
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
        return;
    }

    bool parseError = false;
    std::string parseMsg;
    bool sawFunction = false, sawCallsite = false, sawVtableBase = false, sawVtableIndex = false;

    for (const JsonValue& row : rows->arr) {
        const JsonValue* jKind = row.find("kind");
        const JsonValue* jName = row.find("name");
        const JsonValue* jDatum = row.find("datum");
        const JsonValue* jSlices = row.find("slices");
        if (!jKind || !jKind->isString() || !jSlices || jSlices->type != JsonValue::Type::Array) {
            parseError = true; parseMsg = "a fixture row is missing kind/slices";
            continue;
        }
        const std::string kind = jKind->strVal;
        const std::string rowName = (jName && jName->isString()) ? jName->strVal : std::string("?");

        for (const JsonValue& slice : jSlices->arr) {
            const JsonValue* jBody = slice.find("body");
            const JsonValue* jVerdict = slice.find("verdict");
            const JsonValue* jSliceName = slice.find("name");
            if (!jBody || !jBody->isString() || !jVerdict || !jVerdict->isString()) {
                parseError = true; parseMsg = "a fixture slice is missing body/verdict";
                continue;
            }
            const std::string sliceName = (jSliceName && jSliceName->isString()) ? jSliceName->strVal : std::string("?");
            const std::string pinned = jVerdict->strVal;

            std::vector<uint8_t> body;
            if (!HexToBytes(jBody->strVal, body)) {
                parseError = true; parseMsg = "a fixture slice body is not valid hex";
                continue;
            }

            if (kind == "function") {
                const JsonValue* ch = jDatum ? jDatum->find("content_hash") : nullptr;
                const JsonValue* len = jDatum ? jDatum->find("length") : nullptr;
                std::vector<uint8_t> contentHash;
                if (ch && ch->isString()) HexToBytes(ch->strVal, contentHash);
                uint64_t length = (len && len->isNumber()) ? static_cast<uint64_t>(len->asInt())
                                                          : body.size();
                PlantFunction(out, rowName, sliceName, pinned, body, contentHash, length);
                sawFunction = true;
            } else if (kind == "callsite") {
                const JsonValue* aob = jDatum ? jDatum->find("aob") : nullptr;
                sv::Payload p;
                if (aob && aob->isString()) {
                    try {
                        patch::Pattern pat = patch::ParsePattern(aob->strVal);
                        p.aob = pat.bytes;
                        p.aobMask.resize(pat.mask.size());
                        for (size_t i = 0; i < pat.mask.size(); ++i) p.aobMask[i] = pat.mask[i] ? 1 : 0;
                    } catch (...) {
                        parseError = true; parseMsg = "callsite aob parse failed";
                        continue;
                    }
                }
                PlantCallsite(out, rowName, sliceName, pinned, body, p);
                sawCallsite = true;
            } else if (kind == "vtable_base") {
                const JsonValue* sc = jDatum ? jDatum->find("slot_count") : nullptr;
                const JsonValue* tr = jDatum ? jDatum->find("text_range") : nullptr;
                uint32_t slotCount = (sc && sc->isNumber()) ? static_cast<uint32_t>(sc->asInt()) : 0;
                uint32_t lo = 0x1000, hi = 0x3A01E1A;
                if (tr && tr->type == JsonValue::Type::Array && tr->arr.size() == 2) {
                    lo = static_cast<uint32_t>(tr->arr[0].asInt());
                    hi = static_cast<uint32_t>(tr->arr[1].asInt());
                }
                PlantVtableBase(out, rowName, sliceName, pinned, body, slotCount, lo, hi);
                sawVtableBase = true;
            } else if (kind == "vtable_index") {
                sv::Payload p;
                p.kind = sv::Kind::VtableIndex;
                AssertSlice(out, "vtable_index", rowName, sliceName, pinned,
                            p, /*rva=*/0, /*planted=*/std::vector<uint8_t>{});
                sawVtableIndex = true;
            } else if (kind == "string_anchor" || kind == "instruction_anchor" || kind == "data_slot") {
                // SURFACED divergence (NOT asserted here). The engine's static check
                // for these kinds is a SUPERSET of the cross-impl-agreed (Python/JS)
                // model over the fixture bytes:
                //   string_anchor — the engine ALSO runs the expect_unique .text
                //     LEA-xref check; the agreed model is pure .rdata presence
                //     (survival_checker.py documents the xref as deferred).
                //   instruction_anchor — the engine re-runs the FULL string→LEA
                //     resolver chain (needs a .rdata anchor string + a unique LEA);
                //     the agreed model is the forward shape+disp32-follow primitive.
                //   data_slot — the engine checks "the derivation lands in .data" +
                //     needs the CheckOrdered anchor-RVA threading; the agreed model
                //     checks "the disp32-follow reaches expected_slot_rva".
                // The row is PRESENT in the JSON contract (the JS<->Python pin covers
                // it); the engine<->browser pin for these kinds is the SURFACED design
                // fork returned to the manager. NOT asserted, NOT masked (AP15 — the
                // fixture's ground truth is unchanged).
                LOG_DEBUG_KV(kCategory, "agreement_surfaced",
                    ::kcdx::log::KV("kind", kind.c_str()),
                    ::kcdx::log::KV("slice", sliceName.c_str()),
                    ::kcdx::log::KV("note", "engine static check is a superset of the agreed model — surfaced, not pinned this step"));
            } else {
                parseError = true; parseMsg = "unexpected fixture kind: " + kind;
            }
        }
    }

    // --- Verdict. -------------------------------------------------------------
    if (parseError) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the embedded fixture JSON is malformed (%s) — the cross-language "
            "contract did not parse cleanly; regenerate survival_agreement_fixture.h.",
            parseMsg.c_str());
        LOG_ERROR_KV(kCategory, "selftest_fail", ::kcdx::log::KV("subcheck", "row_parse"));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
        return;
    }

    // Coverage guard (AP15 — the pin must actually exercise the agreeing kinds,
    // never pass vacuously). Every agreeing kind must have been asserted.
    if (!(sawFunction && sawCallsite && sawVtableBase && sawVtableIndex)) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the agreement pin did not cover every agreeing kind "
            "(function=%d callsite=%d vtable_base=%d vtable_index=%d) — the fixture is "
            "missing a kind or the consumer dropped it.",
            sawFunction, sawCallsite, sawVtableBase, sawVtableIndex);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
        return;
    }

    if (out.diverged) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: the C++ engine static verdict DIVERGED from the cross-impl fixture's "
            "pinned verdict — %s. A real engine<->fixture divergence the conformance test "
            "caught (a 3.1/3.2 engine bug); the fixture is the ground-truth (== the Python "
            "== the JS) and is NOT adjusted to mask it (AP15).",
            out.divergeMsg.c_str());
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("subcheck", "agreement"),
            ::kcdx::log::KV("diverge", out.divergeMsg.c_str()));
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
        return;
    }

    std::snprintf(reason, sizeof(reason),
        "PASS — the C++ engine static survival check AGREES with the cross-impl fixture "
        "(== the Python reference == the JS browser checker) on %d planted-PE slices across "
        "the algorithm-identical kinds: function (BLAKE3 body hash — the engine's vendored "
        "BLAKE3 reproduces the pinned content_hash), callsite (AOB unique=Unchanged / "
        "zero=Changed / multi=Ambiguous), vtable_base (N-qword table-shape), vtable_index "
        "(CannotCheck). Each slice's engine verdict == the fixture's pinned verdict "
        "(falsifiable: a divergence on ANY slice fails the row). string_anchor / "
        "instruction_anchor / data_slot are SURFACED — the engine static check is a SUPERSET "
        "of the agreed model over the fixture bytes (the report explains; the engine<->browser "
        "pin for these is the open fork).", out.checked);
    LOG_INFO_KV(kCategory, "selftest_pass",
        ::kcdx::log::KV("slices_checked", (unsigned long long)out.checked));
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-85 survival-agreement");
}

}  // namespace kcdx::survival_agreement_selftest
