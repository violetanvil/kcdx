-- CAP-27 shared state module.
--
-- plugin.lua (the DEFERRED arm) and after.lua (the IMMEDIATE arm + the
-- self-fire/assert) are two SEPARATE Lua chunks — file-local upvalues do not
-- cross between them. They share state through this require'd module: Lua
-- caches it in package.loaded, so both files (and the deferred command's
-- callback, which fires during after.lua's kcdx.console.execute) see the
-- SAME table. The kcdx package.loaders searcher resolves "state" to this
-- plugin's own state.lua (proven by CAP-25), so this stays pure-Lua and
-- within the one-global (`kcdx`) authoring rule — no new global is created.
--
-- One table per command records what its callback observed; both start in the
-- never-fired state so a callback that never dispatches FAILS loudly.

return {
    -- The DEFERRED command (cap27_deferred), registered from plugin.lua.
    deferred = {
        fired = false,   -- did cap27_deferred's callback ever run?
        args  = nil,     -- { args[1], args[2] } the callback saw
        count = nil,     -- #args the callback saw
        raw   = nil,     -- args.raw the callback saw
    },
    -- The IMMEDIATE command (cap27_immediate), registered from after.lua.
    immediate = {
        fired = false,   -- did cap27_immediate's callback ever run?
        args  = nil,
        count = nil,
        raw   = nil,
    },
}
