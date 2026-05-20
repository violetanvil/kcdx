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
//            Always-on (no `.disabled` suffix support); apply BEFORE
//            user plugins. Used today for the BugSplat filename fix.
//
//   User   — plugins/<name>/kcdx.toml
//            Third-party plugins installed by the user. Respect
//            `.disabled` suffix; apply per their priority field.
//
// Walked in this order. Within each engine entry type, the final
// sort key is (source asc, priority asc, name asc) — `Engine`
// sorts before `User`, so cross-source conflicts at the same
// address resolve in the engine fix's favor regardless of
// numeric priority.
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
