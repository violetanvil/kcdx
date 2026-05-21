#pragma once
#include <cstdint>
#include <string>

namespace kcdx::config {

// Where a discovered TOML / plugin came from.
//
// kcdx has TWO discovery roots:
//
//   Engine — kcdx-engine/builtin/<name>/kcdx.toml
//            First-party engine fixes shipped in the kcdx release zip.
//            Apply BEFORE user plugins. Subject to the same
//            enabled = false load_order.toml gate as user plugins —
//            a user can disable a specific engine fix the same way
//            they disable any other plugin (useful as a safety valve
//            if a fix turns out to cause regressions).
//
//   User   — plugins/<name>/kcdx.toml
//            Third-party plugins installed by the user. Subject to
//            the load_order.toml enabled gate; apply per their
//            priority field.
//
// Walked in this order. Within each engine entry type, the final
// sort key is (zone, plugin_priority, plugin_name, source asc, ...)
// — see docs/load-order.md for the full sort. `Engine` sorts before
// `User` at the source tiebreaker level, so cross-source conflicts
// at the same address resolve in the engine fix's favor when other
// keys are equal.
enum class Source : uint8_t {
    Engine = 0,
    User   = 1,
};

// Walks both discovery roots:
//   1) <engineDir>/builtin/<name>/kcdx.toml  (Source::Engine)
//   2) <pluginsDir>/<name>/kcdx.toml         (Source::User)
//
// Parses each file, validates entries, appends valid entries to the
// per-engine global vectors. Sorts each vector by
// (source asc, priority asc, name asc).
//
// On parse errors or schema-validation errors, the offending entry
// is skipped and a log line is emitted; other entries continue to
// load.
void LoadAllConfigs(const std::wstring& pluginsDir);

}  // namespace kcdx::config
