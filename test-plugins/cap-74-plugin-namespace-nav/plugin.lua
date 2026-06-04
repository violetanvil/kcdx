-- CAP-74 plugin.lua — navigable cross-plugin namespace regression.
--
-- The navigable namespace shape on kcdx.plugin:
--
--   kcdx.plugin.<author>.<plugin>.<surface>...
--               ^        ^
--               |        plugin segment — __index on the author-resolver
--               |        userdata; resolves (author, plugin) to a plugin
--               |        handle, or nil for a non-existent plugin (so the
--               |        next .<surface> raises "attempt to index a nil
--               |        value", naming the typoed segment).
--               |
--               author segment — __index on the kcdx.plugin table;
--               resolves <author> to an author-resolver userdata for any
--               loaded plugin's author, or nil for a non-existent author
--               (so the next .<plugin> raises, naming the typoed segment).
--
-- Each dot is a resolution hop against engine-side namespace data
-- (g_plugins). This is the SAME chained-__index mechanism kcdx.hook.<name>
-- uses. The .assets LEAF the chain fronts is step 8's deliverable; this
-- plugin proves the NAVIGATION PRIMITIVE — the author + plugin segments
-- resolve to a real handle, and a miss at either segment teaches.
--
-- Navigation target choice: this plugin's OWN identity (author "ts",
-- plugin "cap_74_plugin_namespace_nav"). self > engine > other means a
-- plugin's own (author, plugin) is guaranteed loaded and resolvable — the
-- reliable known-present target, independent of which OTHER plugins the
-- suite happens to load this session.

local MY_AUTHOR = "ts"
local MY_PLUGIN = "cap_74_plugin_namespace_nav"

-- ====================================================================
-- (1) CAP-74-resolves — the __index chain resolves each segment of
--     kcdx.plugin.<author>.<plugin> to a real engine-side handle.
--
-- FALSIFIABLE: if the author segment fails to resolve, kcdx.plugin
-- .<author> is nil and the .<plugin> hop raises (pcall catches it) →
-- FAIL. If the plugin segment fails, the handle is nil → FAIL. Only a
-- non-nil handle from BOTH hops resolving is PASS.
-- ====================================================================
do
    local ok, handleOrErr = pcall(function()
        return kcdx.plugin[MY_AUTHOR][MY_PLUGIN]
    end)

    if not ok then
        kcdx.test.report("CAP-74-resolves", false,
            "navigating kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. " RAISED: " .. tostring(handleOrErr) .. " — the __index "
            .. "chain failed to resolve a segment for THIS plugin's own "
            .. "(author, plugin) identity, which is guaranteed loaded "
            .. "(self > engine > other). A self-identity miss means the "
            .. "author or plugin resolver did not read g_plugins correctly")
    elseif handleOrErr == nil then
        kcdx.test.report("CAP-74-resolves", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN .. " resolved "
            .. "to nil — the author segment resolved (no raise) but the "
            .. "plugin segment did not return a handle for THIS plugin's "
            .. "own loaded (author, plugin) pair")
    else
        kcdx.test.report("CAP-74-resolves", true,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN .. " resolved "
            .. "the __index chain to a non-nil plugin handle (" ..
            tostring(handleOrErr) .. ") — the author segment resolved to "
            .. "an author resolver and the plugin segment resolved (author, "
            .. "plugin) to this plugin's engine-side handle, each dot a "
            .. "resolution hop against g_plugins")
    end
end

-- ====================================================================
-- (2) CAP-74-bad-author — a non-existent <author> segment yields a
--     TEACHING ERROR (a loud raise naming the segment), not a silent
--     nil that surfaces confusingly downstream (AP14).
--
-- The bad author resolves to nil at kcdx.plugin.<bad>; indexing that
-- nil at the .<anything> hop raises Lua's stock "attempt to index a nil
-- value (field '<bad>')" — the navigation-miss idiom kcdx.hook uses.
--
-- FALSIFIABLE: if the bad author segment returns something indexable
-- (does NOT raise on the .<anything> hop), the miss became a silent nil
-- instead of a teaching error → FAIL.
-- ====================================================================
do
    local BAD_AUTHOR = "ts_no_such_author_xyz"
    local ok, err = pcall(function()
        -- Two hops: kcdx.plugin.<bad> is nil; indexing it raises.
        return kcdx.plugin[BAD_AUTHOR].any_plugin_name
    end)

    local raised = (ok == false)
    if raised then
        kcdx.test.report("CAP-74-bad-author", true,
            "kcdx.plugin." .. BAD_AUTHOR .. ".any_plugin_name RAISED (\""
            .. tostring(err) .. "\") — a non-existent <author> segment "
            .. "resolved to nil and the next hop raised a teaching error "
            .. "naming the segment, never a silent nil (AP14: a navigation "
            .. "miss fails loud)")
    else
        kcdx.test.report("CAP-74-bad-author", false,
            "kcdx.plugin." .. BAD_AUTHOR .. ".any_plugin_name did NOT raise "
            .. "(returned: " .. tostring(err) .. ") — a non-existent "
            .. "<author> segment produced a silent nil that an author could "
            .. "index without error, instead of the teaching error the "
            .. "navigation-miss idiom requires (AP14)")
    end
end

-- ====================================================================
-- (3) CAP-74-bad-plugin — a REAL <author> but a non-existent <plugin>
--     segment yields the teaching error. Proves the SECOND hop's miss
--     path independently of the first: the author resolves, the plugin
--     does not, so .<anything> on the nil plugin raises.
--
-- FALSIFIABLE: a silent nil that does not raise on the .<anything> hop
-- → FAIL (the plugin segment's miss did not teach).
-- ====================================================================
do
    local BAD_PLUGIN = "no_such_plugin_under_ts_xyz"
    local ok, err = pcall(function()
        -- kcdx.plugin.ts resolves (this plugin's author exists);
        -- .<bad plugin> is nil; .<anything> on that nil raises.
        return kcdx.plugin[MY_AUTHOR][BAD_PLUGIN].any_surface
    end)

    local raised = (ok == false)
    if raised then
        kcdx.test.report("CAP-74-bad-plugin", true,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. BAD_PLUGIN .. ".any_surface "
            .. "RAISED (\"" .. tostring(err) .. "\") — a real <author> ("
            .. MY_AUTHOR .. ", which IS loaded) but a non-existent <plugin> "
            .. "resolved the plugin segment to nil and the next hop raised "
            .. "a teaching error, never a silent nil. The second hop's miss "
            .. "path teaches independently of the first")
    else
        kcdx.test.report("CAP-74-bad-plugin", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. BAD_PLUGIN .. ".any_surface "
            .. "did NOT raise (returned: " .. tostring(err) .. ") — a "
            .. "non-existent <plugin> under a real <author> produced a "
            .. "silent nil instead of the teaching error the navigation-miss "
            .. "idiom requires (AP14)")
    end
end

-- ====================================================================
-- (4) CAP-74-is-rejected-coexists — the navigation __index is ADDITIVE.
--     The function member is_rejected shadows the __index metamethod (a
--     raw-table hit never reaches the resolver), so the query surface
--     stays exactly as it was.
--
-- FALSIFIABLE: if kcdx.plugin.is_rejected is no longer a callable
-- function (the __index wiring broke or shadowed the member) → FAIL.
-- We assert it is a function AND that a well-formed query returns the
-- (bool, ...) query shape, not the navigation resolver.
-- ====================================================================
do
    local is_fn = (type(kcdx.plugin.is_rejected) == "function")
    if not is_fn then
        kcdx.test.report("CAP-74-is-rejected-coexists", false,
            "kcdx.plugin.is_rejected is " .. type(kcdx.plugin.is_rejected)
            .. ", not a function — the navigation __index broke or shadowed "
            .. "the is_rejected function member (the navigation surface must "
            .. "be ADDITIVE: function members shadow the resolver)")
    else
        -- Query THIS plugin (loaded normally → not rejected → (false, nil)).
        local ok, rejected = pcall(kcdx.plugin.is_rejected,
            MY_AUTHOR .. "." .. MY_PLUGIN)
        if ok and rejected == false then
            kcdx.test.report("CAP-74-is-rejected-coexists", true,
                "kcdx.plugin.is_rejected is still a callable function and "
                .. "returns the query shape (is_rejected(\"" .. MY_AUTHOR
                .. "." .. MY_PLUGIN .. "\") == false — this plugin loaded "
                .. "normally) — the navigation __index is additive, the "
                .. "function member shadows it")
        else
            kcdx.test.report("CAP-74-is-rejected-coexists", false,
                "kcdx.plugin.is_rejected is a function but did not return "
                .. "the expected query shape (ok=" .. tostring(ok)
                .. ", result=" .. tostring(rejected) .. ") — the __index "
                .. "resolver may be intercepting the is_rejected access "
                .. "instead of letting the function member shadow it")
        end
    end
end

kcdx.log.info("CAP74",
    "ran the kcdx.plugin.<author>.<plugin> navigable-namespace self-test "
    .. "(CAP-74-resolves: self-identity chain resolves; CAP-74-bad-author "
    .. "/ CAP-74-bad-plugin: a miss at either segment raises a teaching "
    .. "error; CAP-74-is-rejected-coexists: the function member still "
    .. "shadows the resolver) — all four reported synchronously at load")
