#pragma once

#include <string>

// Per-plugin `targets.toml` sidecar parser.
//
// A plugin folder MAY ship a `targets.toml` next to its `kcdx.toml`. It
// declares the plugin's OWN author-declared hook/byte targets — the
// "name an un-named site once, share it by name" half of the disassembler
// test (cornerstones.md guarantees 1–3; naming-namespaces.md is the binding
// model). Each `[[target]]` row carries:
//
//   name          = "open_inventory"   # BARE name; the engine derives the
//                                       # <pluginname> prefix from [plugin].name.
//                                       # The author NEVER types their own prefix.
//   <one locator>                       # exactly one of:
//     pattern       = "48 8B .. .."     #   AOB byte signature (expert hatch)
//     rva           = 0x180ABCDE        #   raw RVA literal (expert hatch)
//     address_id    = 42                #   Address Library numeric id
//     target_symbol = "lua_pcall"       #   another already-known target name
//   signature     = "i32 (ptr, i32)"    # optional structured ABI in the
//                                       # kcdx.hook DSL. SHOULD accompany a
//                                       # pattern/rva target (no name carries
//                                       # the ABI), but registration allows ""
//                                       # — we never invent an ABI (AP2).
//
// This is the STORAGE/REGISTRATION step only: each valid row is handed to
// address_library::RegisterAuthorTarget. Precedence resolution and binder
// consumption are LATER steps — this file does not touch ResolveByName.
//
// The file is OPTIONAL: absent → no-op. A malformed row is logged with a
// teaching KV line (plugin + bad field + the rule) and SKIPPED; one bad row
// never kills its siblings.

namespace kcdx::config {

// Parse `<pluginFolder>/targets.toml` (if present) and register every valid
// `[[target]]` row for `pluginName` (the derived namespace prefix — the value
// of this plugin's [plugin].name). No-op when the file is absent. Called from
// config.cpp's discovery walk once a plugin's folder + name are both known.
void LoadTargetsFor(const std::string& pluginFolder,
                    const std::string& pluginName);

}  // namespace kcdx::config
