// kcdx::hook_signature — see header for the surface contract.
//
// Hand-rolled recursive-descent parser. Small enough that pulling
// in a parser-generator would be more code than the parser itself.
// All errors carry a 1-based column index so author-facing log
// lines can underline the offending token.

#include "hook_signature.h"

#include <cctype>
#include <cstring>
#include <set>
#include <string>
#include <unordered_set>

namespace kcdx::hook_signature {

namespace {

// ---- Type token table -----------------------------------------------------

struct TokenEntry {
    const char* token;
    Type        type;
};

constexpr TokenEntry kTokens[] = {
    {"void", Type::Void},
    {"i8",   Type::I8},   {"i16",  Type::I16},  {"i32",  Type::I32},  {"i64",  Type::I64},
    {"u8",   Type::U8},   {"u16",  Type::U16},  {"u32",  Type::U32},  {"u64",  Type::U64},
    {"f32",  Type::F32},  {"f64",  Type::F64},
    {"ptr",  Type::Ptr},
    {"bool", Type::Bool},
    {"wstr", Type::Wstr}, {"cstr", Type::Cstr},
    {"int",  Type::I32},  // common alias; treat as i32
};
constexpr size_t kTokenCount = sizeof(kTokens) / sizeof(kTokens[0]);

bool MatchTypeToken(const std::string& word, Type& out) {
    for (size_t i = 0; i < kTokenCount; ++i) {
        if (word == kTokens[i].token) {
            out = kTokens[i].type;
            return true;
        }
    }
    return false;
}

// ---- Register whitelist ---------------------------------------------------
//
// Win64 fastcall ABI: GPRs and XMM regs that author callbacks can
// reasonably reference. Excludes RSP/RBP (special purpose) and the
// AVX-extended regs (kcdx doesn't target AVX paths yet).

const std::unordered_set<std::string>& GprSet() {
    static const std::unordered_set<std::string> set{
        "rax", "rcx", "rdx", "rbx", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    return set;
}

const std::unordered_set<std::string>& XmmSet() {
    static const std::unordered_set<std::string> set{
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
    };
    return set;
}

bool IsValidRegister(const std::string& s) {
    return GprSet().count(s) > 0 || XmmSet().count(s) > 0;
}

bool IsXmmRegister(const std::string& s) {
    return XmmSet().count(s) > 0;
}

// ---- Reserved arg names ---------------------------------------------------

const std::unordered_set<std::string>& ReservedArgNames() {
    static const std::unordered_set<std::string> set{
        "hook_skip",     // mid-mode skip-original flag
        "hook_retval",   // after-mode return-mutation slot
    };
    return set;
}

// ---- Lexer ----------------------------------------------------------------

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    void SkipWs() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool Peek(char c) {
        SkipWs();
        return pos_ < src_.size() && src_[pos_] == c;
    }

    bool Consume(char c) {
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Read an identifier (alpha/_, then alphanumeric/_). Returns
    // empty string if the next non-ws token isn't an identifier.
    std::string ReadIdent() {
        SkipWs();
        size_t start = pos_;
        if (start >= src_.size()) return {};
        char c = src_[start];
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
            return {};
        }
        size_t p = start + 1;
        while (p < src_.size()) {
            char d = src_[p];
            if (std::isalnum(static_cast<unsigned char>(d)) || d == '_') {
                ++p;
            } else {
                break;
            }
        }
        pos_ = p;
        return src_.substr(start, p - start);
    }

    bool AtEnd() {
        SkipWs();
        return pos_ >= src_.size();
    }

    int Column() const {
        // 1-based column number for error messages.
        return static_cast<int>(pos_) + 1;
    }

private:
    const std::string& src_;
    size_t             pos_ = 0;
};

// ---- Parser ---------------------------------------------------------------

bool ParseArg(Lexer& lex, Arg& out, std::string& err, int& errCol) {
    // Optional `<register>: ` prefix.
    int savedCol = lex.Column();
    std::string first = lex.ReadIdent();
    if (first.empty()) {
        err = "expected type or register name";
        errCol = lex.Column();
        return false;
    }

    if (lex.Consume(':')) {
        // first was a register name. Validate.
        if (!IsValidRegister(first)) {
            err = "unknown register '" + first +
                  "' — expected one of rax, rcx, rdx, rbx, rsi, rdi, "
                  "r8..r15, xmm0..xmm15";
            errCol = savedCol;
            return false;
        }
        out.pinned.name = first;

        // Now expect the type.
        std::string typeWord = lex.ReadIdent();
        if (typeWord.empty()) {
            err = "expected type after register pin '" + first + ":'";
            errCol = lex.Column();
            return false;
        }
        if (!MatchTypeToken(typeWord, out.type)) {
            err = "unknown type '" + typeWord + "' after register pin";
            errCol = lex.Column();
            return false;
        }
        if (out.type == Type::Void) {
            err = "'void' is not valid for an argument";
            errCol = lex.Column();
            return false;
        }

        // XMM regs only carry floats; GPRs only carry non-floats.
        if (IsXmmRegister(out.pinned.name) && !IsFloatType(out.type)) {
            err = "register '" + out.pinned.name +
                  "' is an XMM register and only holds float types "
                  "(f32 / f64), not '" + std::string(TypeToken(out.type)) + "'";
            errCol = savedCol;
            return false;
        }
        if (!IsXmmRegister(out.pinned.name) && IsFloatType(out.type)) {
            err = "register '" + out.pinned.name +
                  "' is a general-purpose register and cannot hold "
                  "float type '" + std::string(TypeToken(out.type)) +
                  "' — use xmm0..xmm15 instead";
            errCol = savedCol;
            return false;
        }
    } else {
        // first was the type itself (no register pin).
        if (!MatchTypeToken(first, out.type)) {
            err = "unknown type '" + first + "'";
            errCol = savedCol;
            return false;
        }
        if (out.type == Type::Void) {
            err = "'void' is not valid for an argument";
            errCol = savedCol;
            return false;
        }
    }

    // Optional arg name.
    std::string maybeName = lex.ReadIdent();
    if (!maybeName.empty()) {
        if (IsReservedArgName(maybeName)) {
            err = "arg name '" + maybeName +
                  "' is reserved by the kcdx engine (used for "
                  "hook-mode-specific semantics); rename your arg";
            errCol = lex.Column();
            return false;
        }
        out.name = maybeName;
    }
    return true;
}

}  // namespace

bool IsFloatType(Type t) {
    return t == Type::F32 || t == Type::F64;
}

const char* TypeToken(Type t) {
    switch (t) {
        case Type::Void: return "void";
        case Type::I8:   return "i8";
        case Type::I16:  return "i16";
        case Type::I32:  return "i32";
        case Type::I64:  return "i64";
        case Type::U8:   return "u8";
        case Type::U16:  return "u16";
        case Type::U32:  return "u32";
        case Type::U64:  return "u64";
        case Type::F32:  return "f32";
        case Type::F64:  return "f64";
        case Type::Ptr:  return "ptr";
        case Type::Bool: return "bool";
        case Type::Wstr: return "wstr";
        case Type::Cstr: return "cstr";
    }
    return "?";
}

bool IsReservedArgName(const std::string& name) {
    return ReservedArgNames().count(name) > 0;
}

ParseResult Parse(const std::string& text) {
    ParseResult r;
    Lexer lex(text);

    // Return type.
    int retCol = lex.Column();
    std::string retWord = lex.ReadIdent();
    if (retWord.empty()) {
        r.error = "expected return type at start of signature";
        r.errorColumn = retCol;
        return r;
    }
    if (!MatchTypeToken(retWord, r.sig.returnType)) {
        r.error = "unknown return type '" + retWord + "'";
        r.errorColumn = retCol;
        return r;
    }

    // Open paren.
    if (!lex.Consume('(')) {
        r.error = "expected '(' after return type";
        r.errorColumn = lex.Column();
        return r;
    }

    // Arg list (possibly empty).
    std::set<std::string> seenNames;
    if (!lex.Peek(')')) {
        while (true) {
            Arg a;
            std::string e;
            int eCol = 0;
            if (!ParseArg(lex, a, e, eCol)) {
                r.error = e;
                r.errorColumn = eCol;
                return r;
            }
            if (!a.name.empty()) {
                if (!seenNames.insert(a.name).second) {
                    r.error = "duplicate arg name '" + a.name + "' — "
                              "named args must be unique";
                    r.errorColumn = lex.Column();
                    return r;
                }
            }
            r.sig.args.push_back(std::move(a));
            if (lex.Consume(',')) continue;
            break;
        }
    }

    if (!lex.Consume(')')) {
        r.error = "expected ',' or ')' after argument";
        r.errorColumn = lex.Column();
        return r;
    }

    if (!lex.AtEnd()) {
        r.error = "unexpected trailing characters after ')'";
        r.errorColumn = lex.Column();
        return r;
    }

    r.ok = true;
    return r;
}

}  // namespace kcdx::hook_signature
