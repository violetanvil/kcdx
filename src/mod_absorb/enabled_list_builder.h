#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Enabled-list builder — STEP 4 of the mod-loader absorb (the production
// takeover). docs/mod-loader-absorb.md "Step 4".
//
// kcdx IS the mod loader: it owns WHICH mods load and in what ORDER. This
// module produces the rebuilt enabled I_Mod* list the SELECT detour hands the
// native MOUNT pass — a synthesized I_Mod record (record_synth::BuildRecord)
// for EVERY enabled discovered mod, in kcdx's resolved load order. The SUPERSET
// model: a vanilla pak mod AND a kcdx plugin alike get a record pointed at
// their folder, so their content mounts identically via the path-driven native
// MOUNT; a kcdx plugin ADDITIONALLY runs its behavior layer through kcdx's own
// loader (unchanged by this step).
//
// SEPARATION FROM THE DETOUR (testability): this module is engine-free — it
// reads the resolved state (the pak-mod registry, the plugin manifests, the
// load_order surface) and returns a std::vector<void*> of synthesized I_Mod*
// pointers, in resolved load order, EXCLUDING disabled mods and DROPPING any
// mod whose record synthesis fails (a null record would crash MOUNT). It never
// touches a live C_ModManager — the SELECT detour (select_detour.cpp) does the
// repoint. This split lets a self-test (cap-55) assert the list's order, count,
// and per-record path field against a synthetic resolved state, with no live
// engine vector.

namespace kcdx::mod_absorb {

// Normalize a mod root path to the native I_Mod record form. A real native
// record's path is "E:\...\3728570527/" — BACKSLASHES for the directory body,
// a trailing FORWARD '/' for the with-slash form. The native
// MOUNT's OpenPacks('<path>/*.pak') needs a consistent separator. Given an
// arbitrary mod-root path (forward or backward separators, with or without a
// trailing separator), produce BOTH forms matching the native shape:
//   slashForm   = backslash body + exactly one trailing '/'
//   noSlashForm = backslash body, no trailing separator
// Exposed so the self-test can assert a mixed-separator input maps to the
// native-matching form.
void NormalizeToNativeRecordForm(const std::string& rootPath,
                                 std::string& slashForm,
                                 std::string& noSlashForm);

// One row of the rebuilt enabled list, for diagnostics + the self-test. The
// resolved-order sequence of these is what BuildEnabledList records into; the
// returned void* vector (BuildEnabledList) is the parallel I_Mod* array the
// engine vector points at.
struct EnabledListEntry {
    std::string loadOrderName;  // "mods.<modid>" (pak mod) or [plugin].name
    std::string id;             // the record's +0x10 id field (modId / plugin name)
    std::string rootPathSlash;  // the record's +0x08 path (native form)
    bool        isPlugin = false;  // true: kcdx plugin; false: vanilla pak mod
    void*       record   = nullptr;  // the synthesized I_Mod* (null if synth failed
                                     //   — such an entry is DROPPED from the void*
                                     //   array, never inserted as a null element)
};

// Build the rebuilt enabled I_Mod* list from the resolved state.
//
// For each enabled discovered mod (pak-mod registry + plugin manifests),
// honoring IsPluginEnabled() (user-disabled OR version-rejected mods excluded),
// ordered by the load_order sort key (zone, priority, orderIndex, name):
//   - populate a ModRecordInput from the mod (pak-mod registry entry or plugin
//     manifest), with the path normalized to the native record form,
//   - synthesize the I_Mod record via record_synth::BuildRecord,
//   - on a non-null record, append the I_Mod* to the returned array; on a null
//     record (vtable unresolved — BuildRecord already logged loud), DROP the
//     mod from the array (never insert a null — a null I_Mod* crashes MOUNT).
//
// `outEntries` (optional, may be null) receives the parallel diagnostic rows in
// the SAME order (one per record actually placed). The returned vector and
// outEntries have the same length.
//
// The returned vector is BY VALUE — the caller (the detour) copies it into a
// process-lifetime store before repointing the engine vector at it (a
// stack/temporary array would dangle when MOUNT walks it later). The
// SYNTHESIZED RECORDS themselves are owned process-lifetime by record_synth.
std::vector<void*> BuildEnabledList(std::vector<EnabledListEntry>* outEntries);

}  // namespace kcdx::mod_absorb
