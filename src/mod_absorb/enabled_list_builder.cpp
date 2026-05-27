#include "enabled_list_builder.h"

#include <algorithm>
#include <cstdint>
#include <tuple>

#include "record_synth.h"
#include "record_validate.h"
#include "pak_mod_registry.h"
#include "../load_order.h"
#include "../log.h"
#include "../plugin_loader.h"

// Enabled-list builder — see enabled_list_builder.h for the surface + the
// SUPERSET model. docs/mod-loader-absorb.md "Step 4".

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// A mod to consider for the rebuilt list, before sorting + synthesis. Carries
// the load-order name (for the sort key + the enabled gate) and the populated
// ModRecordInput (the synthesis source). isPlugin distinguishes the two
// SUPERSET sources for the diagnostic breakdown.
struct Candidate {
    std::string    loadOrderName;
    bool           isPlugin = false;
    ModRecordInput input;
};

// The load_order sort key for one candidate: (zone, priority, orderIndex,
// name). This MIRRORS config.cpp's pluginKey tuple ordering (the SAME key the
// load_order surface resolves) — a vanilla pak mod ("mods.<modid>", after_game,
// priority 0, finite orderIndex) and a plugin (its declared zone/priority,
// orderIndex == INT_MAX) sort into ONE order. Tie-break on the load-order name
// last, exactly as the engine does for plugins.
std::tuple<int, int, int, std::string> SortKey(const std::string& loadOrderName) {
    const auto& eff = kcdx::load_order::Of(loadOrderName);
    return std::tuple<int, int, int, std::string>{
        static_cast<int>(eff.zone),
        eff.priority,
        eff.orderIndex,
        loadOrderName,
    };
}

}  // namespace

void NormalizeToNativeRecordForm(const std::string& rootPath,
                                 std::string& slashForm,
                                 std::string& noSlashForm) {
    // Body: backslashes — a real native I_Mod record's directory body uses
    // backslashes (verified against a live record). Convert every '/' to '\' so
    // a forward-slash or mixed-separator input lands in the one form the native
    // OpenPacks('<path>/*.pak') consumes.
    std::string body = rootPath;
    for (char& c : body) {
        if (c == '/') c = '\\';
    }
    // Strip any trailing separator(s) from the body so we control the suffix.
    while (!body.empty() && (body.back() == '\\' || body.back() == '/')) {
        body.pop_back();
    }
    noSlashForm = body;
    slashForm   = body + "/";  // trailing FORWARD '/' — matches the native record.
}

std::vector<void*> BuildEnabledList(std::vector<EnabledListEntry>* outEntries) {
    if (outEntries) outEntries->clear();

    // ---- 1. Collect candidates from BOTH SUPERSET sources. -----------------
    std::vector<Candidate> candidates;

    // Vanilla pak mods (from the step-3 registry). Each enabled one gets a
    // record pointed at its folder so its *.pak mounts via the native MOUNT.
    for (const PakMod& mod : Registry()) {
        const std::string loName = LoadOrderNameFor(mod.modId);
        if (!kcdx::load_order::IsPluginEnabled(loName)) {
            continue;  // user-disabled OR version-rejected (ApplyVersionGate).
        }
        Candidate c;
        c.loadOrderName = loName;
        c.isPlugin      = false;
        // Path normalized to the native record form (backslash body + trailing
        // '/'); the registry already stores this shape, but normalize so the
        // record form is uniform regardless of how the path was constructed.
        NormalizeToNativeRecordForm(mod.rootPathNoSlash,
                                    c.input.rootPathSlash,
                                    c.input.rootPathNoSlash);
        c.input.id          = mod.modId;
        c.input.displayName = mod.manifest.name;
        c.input.description = mod.manifest.description;
        c.input.author      = mod.manifest.author;
        c.input.version     = mod.manifest.version;
        c.input.createdDate = mod.manifest.createdOn;
        candidates.push_back(std::move(c));
    }

    // kcdx plugins (from the plugin manifests). A plugin ALSO gets a record
    // pointed at its folder so its Data/*.pak (if any) mounts; a pak-less plugin
    // gets a record too (MOUNT finds no *.pak, opens nothing — harmless,
    // uniform, NO special-casing on pak-presence). Its behavior layer
    // (plugin.lua/DLL) runs separately through kcdx's own loader (unchanged by
    // this step). The plugin's load-order name is its [plugin].name.
    for (const kcdx::plugins::PluginManifest& m : kcdx::plugins::g_manifests) {
        if (m.name.empty()) continue;  // anonymous (no [plugin] table) — no record.
        if (!kcdx::load_order::IsPluginEnabled(m.name)) {
            continue;  // user-disabled OR engine-rejected (zone_gate).
        }
        Candidate c;
        c.loadOrderName = m.name;
        c.isPlugin      = true;
        NormalizeToNativeRecordForm(m.folderPath.string(),
                                    c.input.rootPathSlash,
                                    c.input.rootPathNoSlash);
        // A plugin may have no mod.manifest fields — use the [plugin] table's
        // values; an absent field is an empty string, which the record
        // tolerates. id = [plugin].name (the record +0x10 id, the SUPERSET
        // analogue of a pak mod's modId).
        c.input.id          = m.name;
        c.input.displayName = !m.displayName.empty() ? m.displayName : m.name;
        c.input.description = m.description;
        c.input.author      = m.author;
        c.input.version.clear();      // [plugin].version is a packed integer, not
                                      //   a display string; leave empty (the
                                      //   record metadata field is non-load-bearing).
        c.input.createdDate.clear();
        candidates.push_back(std::move(c));
    }

    // ---- 2. Sort into ONE resolved load order. -----------------------------
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return SortKey(a.loadOrderName) < SortKey(b.loadOrderName);
              });

    // ---- 3. Synthesize a record per candidate, in order. -------------------
    std::vector<void*> list;
    list.reserve(candidates.size());
    size_t vanillaPlaced = 0, pluginPlaced = 0, dropped = 0;

    for (const Candidate& c : candidates) {
        void* rec = BuildRecord(c.input);
        if (rec == nullptr) {
            // FAIL LOUD + DROP (never insert a null I_Mod* — it crashes MOUNT
            // on the first virtual dispatch). BuildRecord already logged the
            // unresolved-vtable Error naming the consequence; add the
            // list-context line so the drop is attributable.
            LOG_ERROR_KV(kCat, "enabled_list_drop",
                         kcdx::log::KV("load_order_name", c.loadOrderName),
                         kcdx::log::KV("id", c.input.id),
                         kcdx::log::KV("consequence",
                            "record synthesis returned null (vtable unresolved) "
                            "— mod DROPPED from the rebuilt enabled list (a null "
                            "I_Mod* would crash MOUNT); the mod will NOT load"));
            ++dropped;
            continue;
        }

        // Self-validate the synthesized record against the native invariants
        // (the two in-image vtables + the 8 CryString header fields) BEFORE it
        // can be repointed into the engine. A malformed record is caught + named
        // LOUD here (ValidateSynthRecord logs the field + invariant + the
        // consequence) instead of surfacing later as an opaque native fatal
        // allocation during MOUNT — the keystone crash class. A failing record
        // is DROPPED exactly like a null BuildRecord above: never repointed,
        // and the enabled_list_built count reflects the drop.
        if (!ValidateSynthRecord(rec, c.loadOrderName, c.input.id)) {
            ++dropped;
            continue;
        }

        list.push_back(rec);
        if (c.isPlugin) ++pluginPlaced; else ++vanillaPlaced;

        if (outEntries) {
            EnabledListEntry e;
            e.loadOrderName = c.loadOrderName;
            e.id            = c.input.id;
            e.rootPathSlash = c.input.rootPathSlash;
            e.isPlugin      = c.isPlugin;
            e.record        = rec;
            outEntries->push_back(std::move(e));
        }

        // Verbose per-record line (dev-log-routed via LOG_DEBUG_KV).
        LOG_DEBUG_KV(kCat, "enabled_list_record",
                     kcdx::log::KV("idx", (uint64_t)(list.size() - 1)),
                     kcdx::log::KV("load_order_name", c.loadOrderName),
                     kcdx::log::KV("id", c.input.id),
                     kcdx::log::KV("path", c.input.rootPathSlash),
                     kcdx::log::KV("kind", c.isPlugin ? "plugin" : "pak_mod"));
    }

    LOG_INFO_KV(kCat, "enabled_list_built",
                kcdx::log::KV("count", (uint64_t)list.size()),
                kcdx::log::KV("vanilla", (uint64_t)vanillaPlaced),
                kcdx::log::KV("plugins", (uint64_t)pluginPlaced),
                kcdx::log::KV("dropped", (uint64_t)dropped));

    return list;
}

}  // namespace kcdx::mod_absorb
