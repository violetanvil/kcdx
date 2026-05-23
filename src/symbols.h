#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace kcdx::symbols {

// Cross-plugin symbol table. Populated by kcdx.code{ export = } (and the
// legacy [[trampoline]] export field); consumed by patch_engine / hook_chain
// when an entry uses target_symbol.
//
// NAMESPACE MODEL (naming-namespaces.md) — the SAME <pluginname>.<name> model
// as author-targets; this REPLACES the old globally-unique-or-reject scheme the
// rule's "Known debt to reconcile" section names:
//   - REGISTRATION: the author writes a BARE export name; the engine derives
//     the <pluginname> prefix from the calling plugin's identity and stores the
//     symbol as <pluginname>.<bareName>. The author never types their own
//     prefix. Two plugins may export the same bare name (each lives under its
//     own prefix) — there is NO global-uniqueness rejection. Re-exporting the
//     SAME <plugin>.<name> twice (one plugin, one name) is still a collision.
//   - RESOLUTION (a consumer's target_symbol): a BARE name resolves self >
//     other (symbols have no "engine" tier — the engine seed is the address
//     library, a separate surface); an explicit "<plugin>.<name>" resolves
//     directly and never warns; "kcdx." is reserved. A bare collision (the
//     bare name exported by both self and another plugin) warns ONCE PER
//     SESSION, reusing address_library's shared warn-once dedup.
//   - Address 0 / null exports are rejected.
//   - Launch-time only — registration at the call/apply pass, lookups during
//     pre-flight; never a hook-fire / runtime path.

// Register a BARE export `bareName` -> `addr`, owned by `ownerName` (the
// calling plugin's [plugin].name, the namespace prefix). The engine stores it
// as <ownerName>.<bareName>. Returns true on success, false if THAT fully-
// qualified name is already registered (same plugin re-exporting the same
// name) — on failure the existing entry is left intact and the caller logs the
// collision via OwnerOf. An empty `ownerName` (anonymous caller — console / pak
// Lua) registers the bare name unprefixed (it has no namespace to live under).
bool Register(const std::string& bareName, uintptr_t addr,
              const std::string& ownerName);

// Look up a symbol for a consumer in plugin `owningPlugin` (the consumer's
// [plugin].name, "" for an anonymous / C++ caller with no handle). Applies the
// namespace model: alias substitution, then explicit-prefix-or-self>other
// precedence with the shared warn-once. Returns nullopt if unresolved.
std::optional<uintptr_t> Lookup(const std::string& name,
                                const std::string& owningPlugin = "");

// Look up the owner-plugin-name that registered a symbol, by its FULLY-
// QUALIFIED stored key (<plugin>.<name>, or the bare name for an anonymous
// export). Used for collision-diagnostic log lines. Returns empty string if
// not registered.
std::string OwnerOf(const std::string& fullName);

// Count of registered symbols. Used for log summaries.
size_t Count();

// For diagnostic logging: dump every (name, addr, owner) triple via a
// caller-supplied lambda. Order is unspecified (hash-map iteration).
void ForEach(void (*fn)(const char* name, uintptr_t addr, const char* owner));

}  // namespace kcdx::symbols
