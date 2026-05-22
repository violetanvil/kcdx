-- COMP-10 plugin B's helper.lua — the require'd SIBLING under the bare
-- module name "helper". Plugin A ships a DIFFERENT helper.lua under the
-- SAME bare name returning marker = "A". B's require("helper") MUST resolve
-- to THIS file (marker "B"), never A's — that is the cross-plugin
-- require-cache isolation this fixture guards.
return { marker = "B" }
