-- COMP-10 plugin A's helper.lua — the require'd SIBLING under the bare
-- module name "helper". Plugin B ships a DIFFERENT helper.lua under the
-- SAME bare name returning marker = "B". A's require("helper") MUST resolve
-- to THIS file (marker "A"), never B's — that is the cross-plugin
-- require-cache isolation this fixture guards.
return { marker = "A" }
