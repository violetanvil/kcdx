#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Order persistence — STEP 5 of the mod-loader absorb.
//
// kcdx IS the mod loader: STEP 4 (the SELECT-detour takeover) rebuilds the
// native enabled list from kcdx's discovered mods in resolved order. STEP 5
// makes that order PERSIST + EDITABLE by writing kcdx's resolved load order
// back to BOTH files that describe it:
//
//   - load_order.toml  (the kcdx-owned, user-editable authority): kcdx adds a
//     [[plugin]] row for a newly-discovered pak mod (keyed "mods.<modid>", with
//     the human mod name surfaced as a trailing comment) so the user can SEE +
//     edit it. EXISTING rows — plugin AND pak-mod, including hand-edits — are
//     PRESERVED VERBATIM; kcdx only ADDS missing rows, it never rewrites a row
//     the user owns (the row's zone/priority/enabled ARE the user's authority).
//
//   - mod_order.txt  (the vanilla order file, kept in sync): kcdx writes the
//     resolved PAK-MOD order (the vanilla mods, in kcdx's resolved order, one
//     modid per line) so a future UI reorder + the vanilla file stay in sync.
//
// WRITE-IF-CHANGED (idempotent): each writer serializes the merged/resolved
// result, compares it to the on-disk bytes, and writes ONLY if they differ. A
// steady-state boot (no new mod, no override change, no reorder) writes NOTHING
// — no timestamp churn, no fighting a manual edit. Writing -> re-reading ->
// writing again yields byte-identical output.
//
// FAIL LOUD: a write that FAILS (file unwritable, path missing) logs an ERROR
// naming the file + that the order was NOT persisted (the user's reorder will
// not survive). A write SKIPPED because unchanged logs at DEBUG so the skip is
// visible, never a silent no-op.
//
// SERIALIZATION = HAND-WRITTEN, not a toml++ round-trip. The human mod name is
// surfaced as a trailing '#' comment on the "mods.<modid>" row (load_order.toml
// Read() rejects any unknown key — its allowlist is name/zone/priority/enabled
// — so a "display_name" FIELD would make the file fail its own reader; a
// comment is the only Read()-tolerated way to carry the human name). A full
// toml++ round-trip does NOT preserve comments, which would destroy both the
// file's leading guidance block and the per-row human-name comments. Hand
// writing also gives byte-exact idempotence (no formatter churn) and verbatim
// preservation of every existing row.

namespace kcdx::mod_absorb::order_persist {

// One resolved entry kcdx wants represented in load_order.toml. Built by the
// caller from g_manifests (plugins) + Registry() (pak mods).
struct ResolvedRow {
    std::string loadOrderName;  // the row key: a plugin's [plugin].name, or
                                // "mods.<modid>" for a pak mod.
    std::string humanName;      // the human display name (a pak mod's
                                // mod.manifest <name>); empty for a plugin (a
                                // plugin row carries no surfaced comment). Only
                                // used when ADDING a new pak-mod row.
    bool        isPakMod = false;  // true: a "mods.<modid>" pak-mod row (gets
                                   // the human-name comment on add).
};

// ----------------------------------------------------------------------------
// Pure string serializers — factored out so the self-test can drive them from
// literals with no file I/O.
// ----------------------------------------------------------------------------

// MERGE the resolved rows into the EXISTING load_order.toml text. Returns the
// merged text.
//
//   - Every byte of `existingText` is preserved VERBATIM (leading comment
//     block, every existing [[plugin]] row, every hand-edit, every row for a
//     mod not currently discovered).
//   - For each entry in `rows` whose loadOrderName has NO row in existingText,
//     a new [[plugin]] row is APPENDED. A pak-mod row carries the human name as
//     a trailing comment (`# <humanName>`); a plugin row is bare.
//   - An entry whose name ALREADY has a row is left untouched (add-only — kcdx
//     never overwrites the user's authority over an existing row).
//
// `addedOut` (optional) receives the loadOrderNames that were newly added.
//
// Idempotent: feeding the OUTPUT back as `existingText` with the SAME `rows`
// adds nothing (every name now has a row) -> identical bytes.
std::string MergeLoadOrderToml(const std::string& existingText,
                               const std::vector<ResolvedRow>& rows,
                               std::vector<std::string>* addedOut = nullptr);

// Extract the set of [[plugin]] row names present in load_order.toml text.
// Exposed for the self-test. A "name = \"x\"" line inside a [[plugin]] table
// contributes "x". Bare line scan (NOT a full TOML parse) — it mirrors what
// MergeLoadOrderToml needs (which names already have a row) and tolerates the
// comments toml++ would drop.
std::vector<std::string> ExistingRowNames(const std::string& text);

// Serialize the resolved PAK-MOD order to mod_order.txt body text: a leading
// "# managed by kcdx" comment block, then one modid per line in `order`.
// `order` is the pak-mod modids in kcdx's resolved order (NOT "mods."-prefixed
// — mod_order.txt holds bare modids, the vanilla format).
std::string SerializeModOrderText(const std::vector<std::string>& order);

// True iff the resolved modid `order` DIFFERS from the sequence currently in
// `existingText` (parsed via ParseModOrderText). Comments + blanks are ignored
// in the compare — only the surviving modid SEQUENCE matters, so re-adding the
// "# managed by kcdx" comment to a comment-less vanilla file does not, alone,
// count as a change. Returns true when the order genuinely changed (a write is
// warranted) and when `existingText` is empty/absent but `order` is non-empty.
bool ModOrderDiffers(const std::string& existingText,
                     const std::vector<std::string>& order);

// ----------------------------------------------------------------------------
// File writers — read the on-disk file, merge/serialize, WRITE-IF-CHANGED.
// Each fails LOUD on an I/O error (ERROR naming the file + that the order was
// NOT persisted) and logs a SKIP at DEBUG when unchanged.
// ----------------------------------------------------------------------------

// Merge kcdx's resolved rows into load_order.toml at `loadOrderPath` and write
// if the merged text differs from disk. Builds the ResolvedRow set itself from
// the plugin manifests + the pak-mod registry (in resolved order is irrelevant
// here — load_order.toml is keyed, not ordered). An absent file is treated as
// an empty document (kcdx writes a fresh one with every row).
void WriteLoadOrderToml(const std::filesystem::path& loadOrderPath);

// Write the resolved pak-mod order to <modsDir>/mod_order.txt if it differs
// from the on-disk order. `modsDir` is GameRootDirPath()/"mods". The resolved
// order is the registered pak mods sorted by the SAME load-order key the
// enabled-list build uses, emitting bare modids.
void WriteModOrderTxt(const std::filesystem::path& modsDir);

// The single STEP-5 entry the boot path calls, AFTER load_order::Resolve() +
// the pak-mod version gate have produced the final resolved state. Persists
// BOTH files (each write-if-changed + fail-loud). Independent of whether the
// SELECT-detour takeover fires — persistence reflects the resolved order, not
// the live repoint.
void PersistResolvedOrder();

}  // namespace kcdx::mod_absorb::order_persist
