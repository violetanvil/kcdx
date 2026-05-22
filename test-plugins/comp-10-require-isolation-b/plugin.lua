-- COMP-10 plugin B — cross-plugin require-cache isolation + within-plugin
-- cache hit. The mirror of plugin A, expecting marker "B". See kcdx.toml.
--
--   (1) ISOLATION: require("helper") resolves to THIS plugin's helper.lua
--       (marker "B"), not plugin A's (marker "A").
--   (2) WITHIN-PLUGIN CACHE HIT: a second require("helper") returns the
--       SAME table (same kcdx cache key).

local h1 = require("helper")
local h2 = require("helper")   -- second require -> must be the SAME table

local marker_ok = type(h1) == "table" and h1.marker == "B"
local cache_ok  = (h1 == h2)   -- table identity == within-plugin cache hit

kcdx.test.report("COMP-10-require-isolation-b", marker_ok and cache_ok,
    (marker_ok and cache_ok)
        and ("require('helper') resolved to THIS plugin's helper (marker='B') "
             .. "AND a second require('helper') returned the SAME table "
             .. "(within-plugin cache hit) — cross-plugin isolation + "
             .. "per-plugin cache both hold")
        or  ("isolation/cache FAIL: marker="
             .. tostring(type(h1) == "table" and h1.marker or h1)
             .. " (expected 'B'; a wrong marker means plugin A's helper "
             .. "leaked through the shared bare-name cache), same_table="
             .. tostring(cache_ok) .. " (expected true — within-plugin "
             .. "require should hit the kcdx cache)"))

kcdx.log.info("COMP10",
    "plugin B: require('helper').marker=" .. tostring(h1.marker)
    .. ", second-require-same-table=" .. tostring(cache_ok))
