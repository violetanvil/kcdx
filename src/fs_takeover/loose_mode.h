#pragma once

// Loose-open fopen-mode normalization — translate an engine-supplied mode to one
// kcdx's strict static UCRT accepts (file-system-takeover §5 loose-open path).
//
// kcdx links a static UCRT whose `__acrt_stdio_parse_mode` FAST-FAILS
// (_invalid_parameter → _invoke_watson, c0000409 — process killed, no SEH) on a
// mode the engine's own (lenient) CRT tolerated. The takeover owns EVERY engine
// open, so it must serve the engine's intent under its strict CRT by translating
// the mode — never crashing on it. (KI-0026: the engine opens settings.xml with
// "rbx"; `x` = C11 exclusive-create, valid ONLY on a write base; on a read base
// the strict UCRT rejects it.)

#include <string>

namespace kcdx::fs_takeover {

// Normalize `mode` to a UCRT-valid fopen mode preserving the engine's read/write/
// binary intent. Rule: keep the base (r/w/a) + `+` + b/t; DROP a flag that is
// invalid for that base and inert to the intent — `x` on a read-only base
// (exclusive-create is meaningless on a read). `x` on a writable base (w/a/`+`)
// is valid → kept. Unknown flags are kept (a future strict-CRT-reject is a new
// finding, not silently swallowed). A null/empty mode defaults to "rb". `changed`
// is set true iff a flag was dropped (so the caller can log the normalization).
std::string SanitizeLooseMode(const char* mode, bool& changed);

}  // namespace kcdx::fs_takeover
