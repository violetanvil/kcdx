#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace kcdx::symbols {

// Global string -> address map. Populated by trampoline_engine and (Phase 5+)
// the hook_engine when [[hook]] entries have an export field. Consumed by
// patch_engine and hook_engine when their entries use target_symbol.
//
// Registration model:
//   - Each plugin's export attempt happens once at apply time
//   - Duplicate names are a load-time error: BOTH plugins' entries abort
//     and a clear log line names both
//   - Address 0 / null exports are rejected
//
// Resolution model:
//   - target_symbol lookup: returns nullopt if no entry, populated otherwise
//   - Lookups happen during pre-flight (after Pass-1 concrete locator
//     resolution, before Pass-2 symbol-importer resolution)

// Try to register `name` -> `addr`. Returns true on success, false if `name`
// is already registered. On failure the existing entry is left intact and
// the caller is expected to log a collision warning naming the prior owner.
//
// `ownerName` is recorded so future-failure log lines can identify who
// registered the symbol first.
bool Register(const std::string& name, uintptr_t addr, const std::string& ownerName);

// Look up a symbol. Returns nullopt if not registered.
std::optional<uintptr_t> Lookup(const std::string& name);

// Look up the owner-plugin-name that registered a symbol. Used for
// collision-diagnostic log lines. Returns empty string if not registered.
std::string OwnerOf(const std::string& name);

// Count of registered symbols. Used for log summaries.
size_t Count();

// For diagnostic logging: dump every (name, addr, owner) triple via a
// caller-supplied lambda. Order is unspecified (hash-map iteration).
void ForEach(void (*fn)(const char* name, uintptr_t addr, const char* owner));

}  // namespace kcdx::symbols
