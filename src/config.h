#pragma once
#include <string>

namespace kcdx::config {

// Walks <pluginsDir>/*/kcdx.toml, parses each file, validates entries,
// appends valid entries to kcdx::patch::g_patches, sets kcdx::patch::g_dryRun
// from the first file that supplies a non-default value. Sorts the final
// vector by (priority asc, name asc).
//
// On parse errors or schema-validation errors, the offending entry is skipped
// and a log line is emitted; other entries continue to load.
void LoadAllConfigs(const std::wstring& pluginsDir);

}  // namespace kcdx::config
