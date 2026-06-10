#pragma once

// The dev-tool-unavailable teaching message — ONE source of truth for the
// CONSOLE rendering, shared by kcdx_find and kcdx_dev_inspect.
//
// The discovery tools (kcdx.find / kcdx_dev_inspect) are dev-only: they read
// the DEV reference DB and are gated on dev mode + the dev DB's presence. On the
// gated-off path each surface tells the author how to enable them. The CONSOLE
// surfaces (kcdx_find, kcdx_dev_inspect) print this single-line form to the
// `~` overlay; both reference THIS constant so the two cannot drift.
//
// The Lua kcdx.find binder LOGS a multi-line (`\n`-separated) rendering of the
// SAME content to kcdx-dev.log — a log line wraps differently than a console
// overlay line, so that form is rendered locally in lua_bind_find.cpp by design.
// The CONTENT is identical; only the line-break rendering differs by sink.
//
// Design authority: step-1-find-surface.md §What (the teaching message text).

namespace kcdx::find_dev_teaching {

// The console-overlay (single-line) form. Printed via console::PrintLine on the
// gated-off path by both kcdx_find and kcdx_dev_inspect.
inline constexpr const char* kUnavailableConsole =
    "[kcdx.find] dev tool unavailable. kcdx.find / kcdx_dev_inspect need dev mode "
    "AND the dev reference DB: "
    "1. set dev_mode = true in <game-bin>/kcdx-engine/engine.toml; "
    "2. place reference-dev.sqlite (a separate download, NOT in the release zip) "
    "at <game-bin>/kcdx-engine/data/reference-dev.sqlite. "
    "These are authoring tools — discover a function here, then write your "
    "kcdx.statement.* / kcdx.locator.* code against it.";

}  // namespace kcdx::find_dev_teaching
