-- CAP-67 plugin.lua — the five save/load targets resolve through the Address
-- Library by canonical name to a NON-ZERO address.
--
-- The save/load lifecycle hooks resolve each of their five targets via
-- refdb::ResolveAddrByName (was: runtime AOB scan). kcdx.addr is the Lua-visible
-- snapshot of that same Address Library, built at kcdx-global init: a name
-- appears as a kcdx.memory.pointer userdata ONLY if it resolved on the running
-- build to a NON-ZERO VA AND is verification-state Verified. A name that fails
-- to resolve (unknown, rva==0, unverified) is SKIPPED — kcdx.addr.<name> is nil.
--
-- So `kcdx.addr.<name> ~= nil` IS the falsifiable "resolves by name to a
-- non-zero address" proof. Each row below FAILS if its entity is nil (absent
-- from kcdx.addr) — the regression that would mean the by-name switch lost a
-- target.
--
-- All five rows read kcdx.addr synchronously at plugin load — boot-only, no
-- event wait, no player gesture.

-- The five curated entity names + the test-row id each backs.
local TARGETS = {
    { name = "SaveGame",                    row = "CAP-67-savegame" },
    { name = "LoadGame_wrapper",            row = "CAP-67-loadgame" },
    { name = "PostLoadGame",                row = "CAP-67-postloadgame" },
    { name = "DeleteSavegame",              row = "CAP-67-deletesavegame" },
    { name = "SaveGameRecord_SlotResolver", row = "CAP-67-slotresolver" },
}

for _, t in ipairs(TARGETS) do
    local ptr = kcdx.addr[t.name]
    -- A non-nil entry is a kcdx.memory.pointer userdata wrapping the resolved
    -- VA — present iff the entity resolved by name to a NON-ZERO address and is
    -- verified at the running build. nil = the entity was skipped (resolved to
    -- 0 / unverified) → the by-name resolution failed for this target.
    local resolved = ptr ~= nil

    kcdx.test.report(t.row, resolved,
        resolved
          and ("kcdx.addr." .. t.name .. " is a non-nil pointer ("
               .. tostring(ptr) .. ") — the entity resolved through the Address "
               .. "Library by name to a non-zero address (the save/load hook's "
               .. "resolve-by-name target is live)")
          or  ("kcdx.addr." .. t.name .. " is nil — the entity did NOT resolve by "
               .. "name to a non-zero verified address on this build (absent from "
               .. "the kcdx.addr snapshot); the save/load hook would skip this "
               .. "target"))
end

kcdx.log.info("CAP67",
    "save/load resolve-by-name self-test ran synchronously at load: "
    .. "SaveGame / LoadGame_wrapper / PostLoadGame / DeleteSavegame / "
    .. "SaveGameRecord_SlotResolver each asserted non-nil in kcdx.addr "
    .. "(resolved by name to a non-zero address)")
