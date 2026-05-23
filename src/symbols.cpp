#include "symbols.h"

#include <mutex>
#include <unordered_map>

#include "address_library.h"  // ResolveAlias / WarnBareCollisionShared (shared
                              // alias map + once-per-session collision dedup)

namespace kcdx::symbols {

namespace {

struct Entry {
    uintptr_t   addr = 0;
    std::string owner;     // the registering plugin ([plugin].name); "" = anon
    std::string bareName;  // the author's bare export name (no prefix)
};

std::mutex                              g_mutex;
// Keyed by the FULLY-QUALIFIED name: "<owner>.<bareName>", or the bare name
// when the owner is anonymous ("").
std::unordered_map<std::string, Entry>  g_table;

// Build the fully-qualified storage key for an export. A named owner prefixes;
// an anonymous owner ("") stores the bare name as-is (no namespace to derive).
std::string QualifiedKey(const std::string& owner, const std::string& bareName) {
    if (owner.empty()) return bareName;
    return owner + "." + bareName;
}

// Split a possibly-prefixed reference on the FIRST dot. Returns true + fills
// prefix/rest when a dot is present; false for a bare name. Mirrors
// address_library::SplitPrefixed (the engine parses on the dot — it is
// semantic, naming-namespaces.md).
bool SplitPrefixed(const std::string& name, std::string& prefix,
                   std::string& rest) {
    auto dot = name.find('.');
    if (dot == std::string::npos) return false;
    prefix = name.substr(0, dot);
    rest   = name.substr(dot + 1);
    return true;
}

}  // namespace

bool Register(const std::string& bareName, uintptr_t addr,
              const std::string& ownerName) {
    if (bareName.empty() || addr == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string key = QualifiedKey(ownerName, bareName);
    auto [it, inserted] =
        g_table.try_emplace(key, Entry{addr, ownerName, bareName});
    return inserted;
}

std::optional<uintptr_t> Lookup(const std::string& name,
                                const std::string& owningPlugin) {
    if (name.empty()) return std::nullopt;

    // --- ALIAS substitution (naming-namespaces.md §Aliasing) — BEFORE the
    // self > other walk, using the SAME per-plugin alias map as the address
    // resolver. A local handle; it only fires when the calling plugin owns it,
    // so it never shadows another plugin's bare export.
    std::string aliased =
        kcdx::address_library::ResolveAlias(owningPlugin.c_str(), name.c_str());
    const std::string& eff = aliased.empty() ? name : aliased;

    std::lock_guard<std::mutex> lock(g_mutex);

    // --- EXPLICIT prefixed reference: "<plugin>.<name>" — direct, never warns.
    // (A "kcdx." reference can't match a stored author export — the engine
    // never registers exports under its own reserved root — so it simply
    // misses here, which is the correct "no such symbol" answer.)
    std::string prefix, rest;
    if (SplitPrefixed(eff, prefix, rest)) {
        auto it = g_table.find(eff);
        if (it == g_table.end()) return std::nullopt;
        return it->second.addr;
    }

    // --- BARE reference: resolve self > other (no engine tier for symbols).
    // Walk the table once collecting the calling plugin's own export and the
    // first other-plugin export of this bare name.
    const Entry* selfEntry  = nullptr;
    const Entry* otherEntry = nullptr;
    const Entry* anonEntry  = nullptr;
    for (const auto& [k, e] : g_table) {
        if (e.bareName != eff) continue;
        if (!owningPlugin.empty() && e.owner == owningPlugin) {
            selfEntry = &e;
        } else if (e.owner.empty()) {
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
        std::string winnerOwner = selfEntry->owner;
        std::string shadowed =
            otherEntry ? otherEntry->owner
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
    return it->second.owner;
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_table.size();
}

void ForEach(void (*fn)(const char* name, uintptr_t addr, const char* owner)) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [name, entry] : g_table) {
        fn(name.c_str(), entry.addr, entry.owner.c_str());
    }
}

}  // namespace kcdx::symbols
