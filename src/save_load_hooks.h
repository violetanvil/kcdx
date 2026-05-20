#pragma once

namespace kcdx::save_load_hooks {

// Install the five Phase 6 save/load lifecycle detours.
//
// Phase 6a hooks (verified live 2026-05-19, KCD2 release_1_5_1164953_841):
//
//   - wh::framework::C_SaveGameManager::SaveGame              -> kcdxMessage_SaveGame
//     7-arg function; the engine reads through the 6th arg deep in
//     the body. Plugin-facing message data is the BASENAME of the
//     savegame file (e.g. "save561.whs"), normalized from the engine's
//     full-path argument (%USER%/saves/playline0/...).
//
//   - wh::framework::C_SaveGameManager::LoadGame_wrapper      -> kcdxMessage_PreLoadGame
//     3 args (this, playline, slot). Fires multiple times per
//     user-visible load (engine bootstraps the load through this
//     path); message dispatch is not deduplicated for backward
//     compatibility. Plugins that need ONE fire per user load
//     should listen for kcdxMessage_LoadGameSelected instead.
//     `data` is currently null at this frame; the actual on-disk
//     filename is exposed by the Phase 6b slot resolver below.
//
//   - wh::framework::C_SaveGameManager::PostLoadGame          -> kcdxMessage_PostLoadGame
//     Fires on successful loads only (LoadGame's tail path).
//     Plugin-facing `data` is null.
//
//   - wh::framework::C_SaveGameManager::DeleteSavegame        -> kcdxMessage_DeleteGame
//     Never observed firing in 2026-05-19 testing (no UI delete
//     option in vanilla KCD2); hook installed for completeness.
//
// Phase 6b hook:
//
//   - wh::framework::SaveGameManager::sub::ResolveSlot @ 0x1819DDE78
//     -> kcdxMessage_LoadGameSelected
//     A 24-byte function called inside LoadGame's tail path: takes
//     (sub_object, playline_idx, slot_idx) and returns a
//     SaveGameRecord* in rax. We dereference [record+0x80] to get
//     the savegame basename (live-confirmed with two distinct loads
//     producing "exit.whs" and "save561.whs"). Deduped by record
//     pointer so plugins see one fire per user-visible load even
//     though the engine resolves the record multiple times per load.
//
// Discovery + uniqueness audit + body-wide ABI analysis:
// _research/phase6-save-load/SAVE-LOAD-CANDIDATES.md (Phase 6a) and
// _research/phase6b-recon/SAVE-SELECTION-HOOK.md (Phase 6b).
//
// Called once from dllmain.cpp after kcdx::hooks::Install(). Returns
// true if at least one of the five hooks installed.
bool Install();

}  // namespace kcdx::save_load_hooks
