#include "symbols.h"

#include <mutex>
#include <unordered_map>

#include "log.h"              // LOG_WARN_KV (degenerate-input reject — fail loud)
#include "address_library.h"  // ResolveAlias / WarnBareCollisionShared (shared
                              // alias map + once-per-session collision dedup)

namespace kcdx::symbols {

namespace {

struct Entry {
    uintptr_t   addr = 0;
    // Owning namespace (2-dot model):
    //   ownerAuthor "" + ownerPlugin "" → anonymous (console / pak Lua); the
    //     symbol is stored under its bare name.
    //   ownerAuthor "" + ownerPlugin populated → legacy 1-dot row (the corpus's
    //     state during the in-progress namespace refactor); stored as
    //     <ownerPlugin>.<bareName>.
    //   ownerAuthor populated + ownerPlugin populated → 2-dot row; stored as
    //     <ownerAuthor>.<ownerPlugin>.<bareName>.
    std::string ownerAuthor;
    std::string ownerPlugin;
    std::string bareName;  // the author's bare export name (no prefix)
};

std::mutex                              g_mutex;
// Keyed by the FULLY-QUALIFIED name (the 2-dot or 1-dot composite), or the
// bare name when the owner is anonymous ("" / "").
std::unordered_map<std::string, Entry>  g_table;

// Build the fully-qualified storage key for an export. A populated author +
// plugin produces the 2-dot key; an empty author + populated plugin is the
// 1-dot legacy key; both empty is the anonymous bare key.
std::string QualifiedKey(const std::string& ownerAuthor,
                         const std::string& ownerPlugin,
                         const std::string& bareName) {
    if (ownerPlugin.empty()) return bareName;  // anonymous: bare key
    if (ownerAuthor.empty()) return ownerPlugin + "." + bareName;
    return ownerAuthor + "." + ownerPlugin + "." + bareName;
}

// Render the owner display string for log lines ("<author>.<plugin>",
// "<plugin>" for a legacy 1-dot row, or "" for an anonymous export).
std::string OwnerDisplay(const std::string& ownerAuthor,
                         const std::string& ownerPlugin) {
    if (ownerPlugin.empty()) return {};
    if (ownerAuthor.empty()) return ownerPlugin;
    return ownerAuthor + "." + ownerPlugin;
}

// Split a possibly-qualified reference into its dot-separated segments —
// mirrors address_library::SplitQualified (the engine parses on the dot — it
// is semantic). `count` is the number of segments
// produced; >3 means malformed (overflow).
struct QName {
    std::string segments[3];
    int         count = 0;
};

QName SplitQualified(const std::string& name) {
    QName q;
    if (name.empty()) return q;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            if (q.count < 3) q.segments[q.count] = name.substr(start, i - start);
            ++q.count;
            start = i + 1;
        }
    }
    return q;
}

}  // namespace

bool Register(const std::string& bareName, uintptr_t addr,
              const std::string& ownerAuthor,
              const std::string& ownerPlugin) {
    // FAIL-STATE INSTRUMENTATION (fail loud, never a silent drop): Register
    // returns false for TWO distinct failures — a DEGENERATE input (addr==0
    // or empty bareName) and a DUPLICATE fully-qualified key (try_emplace did
    // not insert). The caller treats false as a COLLISION and logs the prior
    // owner via OwnerOf — but a degenerate-input reject is NOT a collision,
    // and OwnerOf would return "" for it (no such key), so the caller's
    // "already registered by '?'" line mis-describes what went wrong. Warn
    // HERE, naming the real defect, so the degenerate case is distinguishable
    // from a genuine collision. The duplicate-key false below is left
    // SILENT here (the caller owns that diagnostic via OwnerOf — warning here
    // too would double-log the collision). Severity Warn: a dropped export is
    // a recoverable rejection (the region is allocated but unreachable by
    // symbol), not a crash risk.
    if (bareName.empty() || addr == 0) {
        LOG_WARN_KV("SYMBOLS", "register_rejected_degenerate",
            log::KV::BareStr("bare_name", bareName.empty() ? "(empty)"
                                                           : bareName.c_str()),
            log::KV("addr", (void*)addr),
            log::KV("owner_author", ownerAuthor.c_str()),
            log::KV("owner_plugin", ownerPlugin.c_str()),
            log::KV::BareStr("reason",
                bareName.empty()
                    ? "empty export name — nothing to register the symbol "
                      "under; the export is dropped (not a collision)"
                    : "address is 0 — a null/unresolved export cannot be "
                      "registered; the export is dropped (not a collision)"));
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string key = QualifiedKey(ownerAuthor, ownerPlugin, bareName);
    auto [it, inserted] =
        g_table.try_emplace(key, Entry{addr, ownerAuthor, ownerPlugin,
                                       bareName});
    // Duplicate-key reject: leave SILENT here. The caller logs the collision
    // with the prior owner (OwnerOf) — that is the right diagnostic, with the
    // right owner attribution this function cannot add without double-logging.
    return inserted;
}

std::optional<uintptr_t> Lookup(const std::string& name,
                                const std::string& owningAuthor,
                                const std::string& owningPlugin) {
    if (name.empty()) return std::nullopt;

    // --- ALIAS substitution (a local handle resolving only in the
    // declaring plugin's space) — BEFORE the
    // self > other walk, using the SAME per-namespace alias map as the
    // address resolver. A local handle; it only fires when the calling
    // namespace owns it, so it never shadows another plugin's bare export.
    std::string aliased =
        kcdx::address_library::ResolveAlias(owningAuthor.c_str(),
                                            owningPlugin.c_str(),
                                            name.c_str());
    const std::string& eff = aliased.empty() ? name : aliased;

    std::lock_guard<std::mutex> lock(g_mutex);

    QName q = SplitQualified(eff);
    if (q.count == 0 || q.count > 3) return std::nullopt;

    // --- EXPLICIT prefixed reference: a 2-segment "<plugin>.<bare>" or
    // 3-segment "<author>.<plugin>.<bare>" stored key. Direct hash hit; never
    // warns. (A "kcdx." reference can't match a stored author export — the
    // engine never registers exports under its own reserved root — so it
    // simply misses here, which is the correct "no such symbol" answer.)
    if (q.count >= 2) {
        auto it = g_table.find(eff);
        if (it == g_table.end()) return std::nullopt;
        return it->second.addr;
    }

    // --- BARE reference: resolve self > other (no engine tier for symbols).
    // Walk the table once collecting the calling namespace's own export and
    // the first other-namespace export of this bare name.
    const Entry* selfEntry  = nullptr;
    const Entry* otherEntry = nullptr;
    const Entry* anonEntry  = nullptr;
    for (const auto& [k, e] : g_table) {
        if (e.bareName != eff) continue;
        const bool isSelf =
            !owningPlugin.empty() &&
            e.ownerPlugin == owningPlugin &&
            e.ownerAuthor == owningAuthor;
        if (isSelf) {
            selfEntry = &e;
        } else if (e.ownerPlugin.empty()) {
            anonEntry = &e;  // anonymous export stored under the bare key
        } else if (!otherEntry) {
            otherEntry = &e;
        }
    }

    // An anonymous export (no owner) is stored under the bare key and behaves
    // as a same-namespace match for an anonymous consumer; for a named consumer
    // it is just another "other" candidate after self.
    if (owningPlugin.empty() && anonEntry) {
        // Anonymous consumer + anonymous export: a direct bare match.
        return anonEntry->addr;
    }

    bool selfHit  = (selfEntry != nullptr);
    bool otherHit = (otherEntry != nullptr) || (anonEntry != nullptr);

    // Bare collision: the bare name is exported by more than one owner the
    // consumer can see. Warn once (shared dedup with the address resolver).
    if (selfHit && otherHit) {
        std::string winnerOwner =
            OwnerDisplay(selfEntry->ownerAuthor, selfEntry->ownerPlugin);
        std::string shadowed =
            otherEntry ? OwnerDisplay(otherEntry->ownerAuthor,
                                      otherEntry->ownerPlugin)
                       : std::string("<anonymous export>");
        kcdx::address_library::WarnBareCollisionShared(
            eff.c_str(), "self", winnerOwner, shadowed);
    }

    // self > other.
    if (selfEntry)  return selfEntry->addr;
    if (otherEntry) return otherEntry->addr;
    if (anonEntry)  return anonEntry->addr;
    return std::nullopt;
}

std::string OwnerOf(const std::string& fullName) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_table.find(fullName);
    if (it == g_table.end()) return {};
    return OwnerDisplay(it->second.ownerAuthor, it->second.ownerPlugin);
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_table.size();
}

void ForEach(void (*fn)(const char* name, uintptr_t addr, const char* owner)) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [name, entry] : g_table) {
        std::string owner = OwnerDisplay(entry.ownerAuthor, entry.ownerPlugin);
        fn(name.c_str(), entry.addr, owner.c_str());
    }
}

}  // namespace kcdx::symbols
