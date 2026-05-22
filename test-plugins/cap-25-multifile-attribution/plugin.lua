-- CAP-25 plugin.lua (the ENTRYPOINT) — multi-file plugin + complete source
-- attribution. Minimal by design: it require()s the sibling helper (the
-- idiomatic Lua form; the kcdx searcher resolves "helper" to this plugin's
-- own helper.lua) and logs that it loaded. The HELPER does the real work
-- (subscribe + publish + assert) — it is the file under test, because the
-- regression is whether a kcdx.* call made from inside a REQUIRE'D source
-- resolves to this plugin.
--
-- require("helper") runs the helper synchronously at THIS plugin's load:
-- the helper's top-level kcdx.on subscription is therefore registered at
-- plugin-load time, before any input_loaded callback fires (deterministic
-- ordering — see helper.lua).

require("helper")

kcdx.log.info("CAP25",
    "entrypoint loaded; require('helper') resolved the sibling helper "
    .. "(which owns the CAP-25 subscribe + deferred publish + assert)")
