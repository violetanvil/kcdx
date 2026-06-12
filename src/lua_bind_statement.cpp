// kcdx.statement — Lua-facing static-bytes modification (sub-verb surface).
//
// A game-mod authoring surface that modifies the game's BYTES STATICALLY: the
// author names a target statement + a kcdx.op.* value, and the engine emits the
// op's bytes and writes them in place. The modified bytes then execute NATIVELY
// every call — ZERO per-call cost, no Lua dispatch. This is the static-bytes
// sibling of kcdx.hook.* (which pays a per-call Lua dispatch). Use kcdx.statement
// when the behavior is static and native-speed execution is wanted; use
// kcdx.hook when per-call Lua logic is needed.
//
//   kcdx.statement.replace_with(module, target, [locator], op, [opts])
//     -- accepts ONLY a static op (a kcdx.op.* value). target is a
//        kcdx.functions.* reference VALUE or a name string; [locator] is an
//        optional kcdx.locator.* value (defaults to function_entry()).
//   kcdx.statement.insert_before(module, target, locator, callback, [opts])
//     -- callback-only; locator REQUIRED.
//   kcdx.statement.insert_after (module, target, locator, callback, [opts])
//     -- callback-only; locator REQUIRED.
//
// NO before/after/around/replace sub-verbs — those describe callback-ordering
// relative to an original call, which has no static-bytes analog.
//
// THE POSITIONAL CONTRACT (lua-api-surface.md rule 4 / 4a, mirrors kcdx.hook):
//   - `module` is the REQUIRED first positional on every sub-verb. No default.
//   - `target` (2nd positional) is a function NAME string OR a kcdx.functions.*
//     reference value. The name carries the address (the disassembler test,
//     cornerstones.md — no author hex). For replace_with the COMMON case names a
//     real statement via a real locator (first_call_to / return_value / …).
//   - `[locator]` (optional positional) is a kcdx.locator.* value. Omitted on
//     replace_with → function_entry() (replace_with defaults the locator to
//     function_entry()). REQUIRED on insert_before/insert_after ("insert before
//     what?" has no default).
//   - For replace_with the next positional is the REQUIRED `op` (a kcdx.op.*
//     value). For insert_before/after the next positional is the REQUIRED
//     callback (a function).
//   - `[opts]` — a trailing optional table (name, description).
//
// THE APPLY PATH (replace_with), end to end:
//   1. Resolve the STATEMENT: refdb::ResolveStatementByName(target, locator) →
//      a StatementResolution carrying the statement's decoded kind, its module
//      byte offset (byte_range_start) + byte span (byte_range_len).
//   2. KIND CHECK at registration: the op declares the statement kind it applies
//      to (a branch op requires a branch statement; a call op a call; …). A
//      mismatch is a LOUD teaching error naming the actual + required kind
//      (errors-that-teach) — the engine does NOT gate on
//      semantic-purpose correctness (the author's call).
//   3. EMIT the op's bytes at APPLY time against (statement kind, byte_range_len):
//      a DETERMINATE op (replace_with_noop / skip_call_void / return-const /
//      …) emits an exact statement-bytes-INDEPENDENT sequence. A DEFERRED op
//      (always_take_branch / invert_branch_condition / replace_call_target /
//      replace_assignment_value / replace_compare_constant) needs the apply-time
//      statement's actual rel32 displacement / call-target / operand encoding,
//      which the statement-resolution layer does NOT expose — so the apply path
//      surfaces a CLEAR not-yet-emittable deferral, never a fabricated byte
//      (a guessed branch/call/operand byte is a silent defect).
//   4. WRITE or TRAMPOLINE: the emitted bytes fit byte_range_len → a same-length
//      byte rewrite at the statement's VA, written through the EXISTING bytes
//      machinery (patch::ApplyPatch on a PatchEntry whose resolvedVa is the
//      statement VA). The bytes EXCEED the range → the engine trampolines (lift
//      to a ±2GB-adjacent allocation, rel32 redirect). The author NEVER sees a
//      "doesn't fit" failure (the engine pads-and-trampolines so the author
//      never sees a doesn't-fit failure).
//
// THE DEFERRED-APPLY MODEL (Kind::Statement). Like kcdx.bytes / kcdx.hook, the
// surface VALIDATES immediately (the author gets (nil, err) in straight-line
// code on a bad call) and DEFERS the resolve+emit+write to the end-of-zone
// ApplyZone pass, so the conflict engine sees every plugin's intent before any
// byte changes. The Kind::Statement handler runs in unified load order, the same
// conflict-engine participation as bytes/hooks (hook-engine.md).
//
// insert_before / insert_after: the surface + registration are built here; the
// engine's statement-locator capture-thunk apply path is NOT yet wired (the same
// path kcdx.hook.insert_* deferred at step 4), so an insert is enqueued and fails
// LOUD at apply with a teaching reason (NOT faked green). See SURFACED at file
// end.

#include "lua_bind_statement.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_bind_functions.h"  // ReadFunctionRef (arg-2-type dispatch: reference value target)
#include "lua_bind_helpers.h"    // FindUnknownKey (shared unknown-key gate)
#include "lua_bind_locator.h"    // ReadLocator (arg-type dispatch: locator positional)
#include "lua_bind_op.h"         // ReadOp / OpView / OpKindSatisfies / EmitDeterminate (the op-read seam)
#include "lua_registry.h"
#include "patch_engine.h"        // PatchEntry + ApplyPatch (the byte-write machinery, reused)
#include "refdb.h"               // ResolveStatementByName + ResolveByName/ResolveAddrByName (statement VA)

namespace kcdx::lua_bind_statement {

namespace {

// =============================================================================
// The queued payload (engine state, surface-independent).
// =============================================================================
//
// The payload type is the PUBLIC StatementRegistration (lua_bind_statement.h)
// — the one shape both authoring surfaces (these Lua verbs and the C++
// kcdxStatementInterface thunks) fill and hand to QueueStatement, and the one
// shape the apply handler below consumes. A replace_with intent carries
// hasOp=true; an insert_* intent carries insertPending=true and fails loud at
// apply (the capture-thunk path is unwired).

// =============================================================================
// The apply handler (Kind::Statement).
// =============================================================================
//
// Runs in the end-of-zone apply pass (lua_registry::ApplyZone), in unified load
// order. For replace_with: resolves the statement (refdb), emits the op's bytes,
// and writes them in place (a same-length rewrite through the EXISTING bytes
// machinery) or surfaces a deferral when the op needs apply-time statement bytes
// the resolution layer does not expose, or when the emitted bytes exceed the
// statement range (the trampoline path for a statement replacement is a tracked
// gap — see SURFACED). On any miss the handle goes Failed with a teaching reason.
bool ApplyStatementEntry(kcdx::lua_registry::Entry& entry,
                         std::string& reason_out) {
    auto sp = std::static_pointer_cast<StatementRegistration>(entry.payload);
    StatementRegistration* p = sp.get();
    if (!p) {
        reason_out = "internal error: statement entry payload is null";
        return false;
    }

    // insert_before / insert_after: the engine's curated-statement capture-thunk
    // apply path is not yet wired (the same unwired path kcdx.hook.insert_*
    // defers at step 4). Fail LOUD — never a faked-green install.
    if (p->insertPending) {
        reason_out =
            "kcdx.statement.insert_before/insert_after on a statement locator is "
            "not yet wired in the engine — the curated-statement capture-thunk "
            "apply path lands in a later step. The sub-verb + registration "
            "validate here; the install is deferred. (Use kcdx.statement."
            "replace_with for a static-bytes modification at a resolved "
            "statement until then.)";
        return false;
    }

    if (!p->hasOp) {
        reason_out = "internal error: replace_with entry carries no op";
        return false;
    }

    // 1. Resolve the statement against the curated reference DB.
    refdb::CallerContext ctx;
    ctx.callType = "kcdx.statement";  // attribution tag for the resolve logs.
    refdb::StatementResolution res =
        refdb::ResolveStatementByName(p->targetName, p->locator, ctx);
    if (!res.found) {
        // refdb logged the detail; surface the reason token so the author can
        // tell a deploy-state miss (function_no_statements / db_not_loaded) from
        // a real one (name_unknown / locator_no_match / call_to_ambiguous).
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the statement did "
            "not resolve (reason=" + res.reason + ") for target '" +
            p->targetName + "'. Check the target name + the locator against "
            "the curated reference DB (the statement tables must be deployed).";
        return false;
    }

    // 2. KIND CHECK — the op must apply to the resolved statement's kind. A
    //    mismatch is a loud teaching error naming the actual + required kind.
    //    (Re-checked here at apply: the registration-time check sees the
    //    op's kind but not the RESOLVED statement's kind, which only the apply
    //    pass has.)
    if (!kcdx::lua_bind_op::OpKindSatisfies(p->op, res.kind)) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the op '" +
            p->op.op_label + "' requires a " + p->op.required_label +
            " statement; the resolved statement is a `" + res.kind +
            "`. Pick an op that applies to a `" + res.kind +
            "` statement, or a locator that resolves to a " +
            p->op.required_label + " statement.";
        return false;
    }

    // 3. The byte span is required to emit + to pick same-size vs trampoline.
    if (!res.has_byte_range_len) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the resolved "
            "statement carries no byte_range_len — the engine cannot size the "
            "byte emit. (The curated statement row is missing its byte span.)";
        return false;
    }
    const int64_t range = res.byte_range_len;

    // A DEFERRED op needs the apply-time statement's actual bytes (a rel32
    // displacement, a resolved call target, an operand encoding) which the
    // statement-resolution layer does NOT expose. Surface the not-yet-emittable
    // deferral — NEVER a fabricated byte. The determinate ops emit a
    // statement-bytes-independent sequence and proceed.
    if (!p->op.emit_determinate) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the op '" +
            p->op.op_label + "' is not yet emittable — its final bytes need the "
            "apply-time statement's own bytes (a branch displacement / a resolved "
            "call target / an operand encoding), which the statement-resolution "
            "layer does not yet expose. The determinate ops (replace_with_noop / "
            "skip_call_void / replace_with_return / replace_return_value / "
            "skip_call_return_value / never_take_branch) emit from kind+range "
            "alone and apply today; the site-dependent emit lands in a later "
            "step. Surfaced, not guessed.";
        return false;
    }

    // 3b. Emit the determinate bytes (the SAME emit :emit_for exposes to Lua).
    std::vector<uint8_t> bytes =
        kcdx::lua_bind_op::EmitDeterminate(p->op, range);
    if (bytes.empty()) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the op '" +
            p->op.op_label + "' emitted no bytes for a " + std::to_string(range) +
            "-byte statement (internal: a determinate op must emit). Report a "
            "kcdx issue.";
        return false;
    }

    // 4. WRITE or TRAMPOLINE. The emitted bytes EXCEEDING the statement range is
    //    the trampoline case (the engine pads-and-trampolines so the author never
    //    sees a doesn't-fit failure). The trampoline path for a STATEMENT replacement (lift the
    //    statement to a ±2GB allocation + rel32 redirect) is a tracked gap — the
    //    determinate ops the test exercises FIT their range, so the same-size
    //    write path is what lands now. A non-fitting op surfaces the deferral
    //    rather than a partial/over-long in-place write (which would corrupt the
    //    following instruction). See SURFACED.
    if (static_cast<int64_t>(bytes.size()) > range) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the op '" +
            p->op.op_label + "' emits " + std::to_string(bytes.size()) +
            " bytes which exceed the statement's " + std::to_string(range) +
            "-byte range — this is the auto-trampoline case (lift the statement "
            "to a ±2GB allocation + rel32 redirect), which is not yet wired for "
            "statement replacement. The fitting determinate ops apply today; the "
            "trampoline path lands in a later step. Surfaced, not a partial "
            "write.";
        return false;
    }

    // The bytes fit: a same-length byte rewrite at the statement's VA. NOP-pad to
    // the full statement range so the rewrite is length-preserving (the bytes
    // machinery requires replacement length == the written span; the emit already
    // pads return/value ops to the range, but a shorter determinate emit — e.g.
    // never_take_branch over a longer range — is padded here to the full range).
    while (static_cast<int64_t>(bytes.size()) < range) bytes.push_back(0x90);

    // The statement's absolute VA. byte_range_start is the statement's MODULE
    // byte offset; the module base = (function VA) - (function RVA), both from
    // the existing refdb public API (no new WhgameBase accessor, no hardcoded
    // address — resolved by name through the database). statementVA = base + byte_range_start.
    if (!res.has_byte_range_start) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the resolved "
            "statement carries no byte_range_start — no write address. (The "
            "curated statement row is missing its module offset.)";
        return false;
    }
    refdb::NameResolution fn = refdb::ResolveByName(p->targetName, ctx);
    const uintptr_t fnVa = refdb::ResolveAddrByName(p->targetName, ctx);
    if (!fn.found || fnVa == 0) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the target function '" +
            p->targetName + "' resolved a statement but not a live address "
            "(WHGame.dll not mapped, or an address-less kind). Cannot write.";
        return false;
    }
    const uintptr_t moduleBase = fnVa - static_cast<uintptr_t>(fn.rva);
    const uintptr_t statementVa =
        moduleBase + static_cast<uintptr_t>(res.byte_range_start);

    // Route the write through the EXISTING bytes machinery: a PatchEntry whose
    // resolvedVa is the statement VA (the carrier patch::Resolve uses directly,
    // skipping locator resolution) + the emitted replacement. Same-length write,
    // idempotent (a re-apply of the identical bytes is a clean skip).
    auto pe = std::make_shared<kcdx::patch::PatchEntry>();
    pe->sourceFile   = "<lua-statement>";
    pe->name         = p->name;
    pe->description  = p->description;
    pe->module       = p->module;
    pe->pluginAuthor = p->owningAuthor;
    pe->pluginName   = p->owningPlugin.empty() ? std::string("<lua-statement>")
                                               : p->owningPlugin;
    pe->resolvedVa   = statementVa;   // skip locator resolution; write here.
    pe->offset       = 0;
    pe->replacement  = bytes;
    pe->idempotent   = true;

    // `original` is the live pre-image read at the statement VA — required only
    // to satisfy patch::Resolve's original.size()==replacement.size() length
    // precondition. The protective compare is replacement-vs-site, not
    // original-vs-site: patch::ApplyResolvedPatch idempotently SKIPS when the
    // site already holds `replacement`, and a foreign mod's bytes at the site
    // reject loud (KI-0017). Read with the same primitive the engine uses to
    // read a code site (patch_engine.cpp VerifyOriginalAtAddr).
    {
        const auto* siteBytes = reinterpret_cast<const uint8_t*>(statementVa);
        pe->original.assign(siteBytes, siteBytes + bytes.size());
    }

    bool ok = false;
    try {
        ok = kcdx::patch::ApplyPatch(*pe);
    } catch (const std::exception& ex) {
        reason_out = std::string("kcdx.statement.replace_with '") + p->name +
                     "': byte write failed: " + ex.what();
        return false;
    }
    if (!ok) {
        reason_out =
            "kcdx.statement.replace_with '" + p->name + "': the byte write was "
            "rejected at the statement VA (see the engine log for the patch-apply "
            "diagnostic).";
        return false;
    }

    LOG_INFO_KV("STATEMENT", "replace_with applied",
        ::kcdx::log::KV("name", p->name),
        ::kcdx::log::KV("target", p->targetName),
        ::kcdx::log::KV("op", p->op.op_label),
        ::kcdx::log::KV("stmt_kind", res.kind),
        ::kcdx::log::KV("byte_range_len", static_cast<long long>(range)),
        ::kcdx::log::KV("wrote_bytes", static_cast<long long>(bytes.size())));
    return true;
}

// =============================================================================
// Shared registration core.
// =============================================================================

// The recognized [opts] key set. An unknown string key fails loud (a typo'd
// `nmae=` would otherwise vanish — never a silent drop).
const char* kOptsKnown[] = { "name", "description" };

std::string OptString(lua_State* L, int optsIdx, const char* key) {
    if (optsIdx == 0) return "";
    lua_getfield(L, optsIdx, key);
    std::string out;
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

// Read the REQUIRED `module` first positional. Returns false + leaves a
// (nil, err) on the stack when missing/non-string.
bool ReadModule(lua_State* L, const char* verb, std::string& out) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.statement.%s(module, target, ...): `module` (the 1st "
            "positional) is REQUIRED — the DLL the target lives in (e.g. "
            "\"WHGame.dll\"). There is no default module.", verb);
        return false;
    }
    out = lua_tostring(L, 1);
    return true;
}

// Resolve the `target` (2nd positional) to a curated function NAME. A
// kcdx.functions.* reference value → its carried name; a string → itself. Fills
// `nameOut`; returns false + leaves (nil, err) on a bad/unsupported target.
bool ReadTargetName(lua_State* L, const char* verb, int targetIdx,
                    const std::string& reportName, std::string& nameOut) {
    kcdx::lua_bind_functions::FunctionRefView ref;
    if (kcdx::lua_bind_functions::ReadFunctionRef(L, targetIdx, ref)) {
        // A reference VALUE. A game reference carries a canonical name the
        // statement-resolution path takes. A by_id / address-only reference has
        // no name string to resolve a statement by — surface that.
        if (!ref.name.empty()) {
            nameOut = ref.name;
            return true;
        }
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.statement.%s '%s': the kcdx.functions reference passed as the "
            "target carries no resolvable name (a by_id or address-only "
            "reference). Pass a named reference (kcdx.functions.<DLL>.<Name>) or "
            "the name string directly.", verb, reportName.c_str());
        return false;
    }
    if (lua_type(L, targetIdx) == LUA_TSTRING) {
        nameOut = lua_tostring(L, targetIdx);
        return true;
    }
    lua_pushnil(L);
    lua_pushfstring(L,
        "kcdx.statement.%s '%s': the target (2nd positional) must be a curated "
        "function NAME string or a kcdx.functions.* reference value — got %s.",
        verb, reportName.c_str(), luaL_typename(L, targetIdx));
    return false;
}

// [opts] unknown-key gate (fail loud). optsIdx == 0 → no opts table. Returns ""
// on OK or a diagnostic string (the caller pushes (nil, err)).
std::string OptsGate(lua_State* L, const char* verb, int optsIdx) {
    if (optsIdx == 0) return "";
    if (!lua_istable(L, optsIdx)) {
        return std::string("the trailing [opts] argument must be a table "
                           "(e.g. { name = \"...\" })");
    }
    std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
        L, optsIdx, kOptsKnown, sizeof(kOptsKnown) / sizeof(kOptsKnown[0]));
    if (!bad.empty()) {
        return std::string("unrecognized option key '") + bad +
               "' in [opts] — not a recognized kcdx.statement option (check for "
               "a typo). Recognized: name, description.";
    }
    return "";
}

// Stamp the Lua-surface specifics onto a verb-built registration (the owning
// plugin from the calling script's identity, the call site, the synthesized
// default name), queue it through the shared QueueStatement seam (the ONE
// payload-construction + registry-append home, shared with the C++ interface
// thunks), and push the handle (or (nil, err)).
int QueuePayload(lua_State* L, StatementRegistration& reg, const char* verb) {
    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);
    reg.owningAuthor = owner.author;
    reg.owningPlugin = owner.plugin;
    reg.callSiteFile = callSiteFile;
    reg.callSiteLine = callSiteLine;
    if (reg.name.empty()) reg.name = std::string("lua_statement_") + verb;

    std::string err;
    uint64_t handleId = QueueStatement(reg, &err);
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

// =============================================================================
// kcdx.statement.replace_with(module, target, [locator], op, [opts])
// =============================================================================
//
// The optional locator floats between target and op: arg 3 is EITHER the op (a
// kcdx.op.* value, no locator → function_entry default) OR the locator (a
// kcdx.locator.* value, op shifts to arg 4). Disambiguated by type
// (lua-api-surface.md rule 4).
int Lua_ReplaceWith(lua_State* L) {
    const char* verb = "replace_with";
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // arg 3 is EITHER the op (no locator) OR a locator (op at arg 4). Probe for
    // a locator value at arg 3 first.
    refdb::StatementLocator locator;  // defaults to FunctionEntry.
    int opIdx, optsIdx;
    {
        kcdx::lua_bind_locator::LocatorView lv;
        const bool arg3IsLocator =
            kcdx::lua_bind_locator::ReadLocator(L, 3, lv);
        if (arg3IsLocator) {
            // The common statement case uses a real statement locator. The
            // LocatorView the cross-binder seam exposes carries only the kind
            // label + is_function_entry; the apply path needs the full
            // StatementLocator descriptor, which is the userdata payload itself.
            // ReadLocator does not surface it, so re-read the descriptor from the
            // userdata via the locator binder's payload accessor.
            const refdb::StatementLocator* loc =
                kcdx::lua_bind_locator::ReadLocatorDescriptor(L, 3);
            if (loc) locator = *loc;
            opIdx   = 4;
            optsIdx = lua_istable(L, 5) ? 5 : 0;
        } else {
            opIdx   = 3;   // no locator; op is arg 3, locator stays function_entry.
            optsIdx = lua_istable(L, 4) ? 4 : 0;
        }
    }

    // The REQUIRED op — a kcdx.op.* value (NOT a callback; replace_with is
    // static-op only).
    kcdx::lua_bind_op::OpView op;
    if (!kcdx::lua_bind_op::ReadOp(L, opIdx, op)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.statement.replace_with(module, target, [locator], op, "
            "[opts]): the `op` (a kcdx.op.* value) is REQUIRED — got %s at the "
            "op position. replace_with takes a STATIC op (e.g. "
            "kcdx.op.replace_with_noop(), kcdx.op.return_const(0)); a per-call "
            "callback is kcdx.hook.* (kcdx.statement is zero-per-call static "
            "bytes). Call shape: kcdx.statement.replace_with(\"WHGame.dll\", "
            "\"IsInCombat\", kcdx.op.return_const(0)).",
            luaL_typename(L, opIdx));
        return 2;
    }

    std::string optsErr = OptsGate(L, verb, optsIdx);
    if (!optsErr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.statement.%s: %s", verb, optsErr.c_str());
        return 2;
    }

    StatementRegistration reg;
    reg.module      = module;
    reg.locator     = locator;
    reg.op          = op;
    reg.hasOp       = true;
    reg.name        = OptString(L, optsIdx, "name");
    reg.description = OptString(L, optsIdx, "description");

    std::string reportName = reg.name.empty() ? std::string("replace_with")
                                              : reg.name;
    if (!ReadTargetName(L, verb, /*targetIdx=*/2, reportName, reg.targetName))
        return 2;

    return QueuePayload(L, reg, verb);
}

// =============================================================================
// kcdx.statement.insert_before / insert_after (module, target, locator,
//                                               callback, [opts])
// =============================================================================
//
// Callback-only (NO static-op form). locator REQUIRED ("insert before what?"
// has no default). The surface + registration are built; the engine's
// statement-locator capture-thunk apply path is NOT yet wired, so the entry is
// enqueued with insertPending and fails LOUD at apply (NOT faked green) — the
// same honest deferral kcdx.hook.insert_* took at step 4. The callback ref is
// NOT taken yet (the apply path that would invoke it is unwired); when the
// capture-thunk path lands, this verb takes the GC-safe ref + the apply handler
// installs the insert. Deferring the ref keeps the unwired path from holding a
// registry ref it never releases.
int InsertVerb(lua_State* L, const char* verb) {
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // locator = arg 3 (REQUIRED).
    kcdx::lua_bind_locator::LocatorView lv;
    if (!kcdx::lua_bind_locator::ReadLocator(L, 3, lv)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.statement.%s(module, target, locator, callback, [opts]): the "
            "`locator` (3rd positional) is REQUIRED — a kcdx.locator.* value "
            "naming the statement to insert at (e.g. "
            "kcdx.locator.first_call_to(\"IsInCombat\")). \"Insert before what?\" "
            "has no default.", verb);
        return 2;
    }
    if (!lua_isfunction(L, 4)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.statement.%s(module, target, locator, callback, [opts]): the "
            "callback (4th positional, a function) is REQUIRED — got %s.",
            verb, luaL_typename(L, 4));
        return 2;
    }
    const int optsIdx = lua_istable(L, 5) ? 5 : 0;
    std::string optsErr = OptsGate(L, verb, optsIdx);
    if (!optsErr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.statement.%s: %s", verb, optsErr.c_str());
        return 2;
    }

    StatementRegistration reg;
    reg.module        = module;
    reg.insertPending = true;
    reg.name          = OptString(L, optsIdx, "name");
    reg.description   = OptString(L, optsIdx, "description");

    std::string reportName = reg.name.empty() ? std::string(verb) : reg.name;
    if (!ReadTargetName(L, verb, /*targetIdx=*/2, reportName, reg.targetName))
        return 2;
    // The locator descriptor is read for completeness (the apply path will need
    // it when the capture-thunk path lands); the not-yet-wired apply ignores it.
    const refdb::StatementLocator* loc =
        kcdx::lua_bind_locator::ReadLocatorDescriptor(L, 3);
    if (loc) reg.locator = *loc;

    return QueuePayload(L, reg, verb);
}

int Lua_InsertBefore(lua_State* L) { return InsertVerb(L, "insert_before"); }
int Lua_InsertAfter (lua_State* L) { return InsertVerb(L, "insert_after");  }

}  // namespace

// The single queue seam both surfaces share (declared in lua_bind_statement.h).
// Payload construction + the Kind::Statement registry append live HERE only:
// the Lua verbs above and the C++ kcdxStatementInterface thunks
// (src/statement_interface.cpp) each validate their own surface's arguments,
// fill a StatementRegistration, and call this — neither builds a registry
// entry itself, so the two surfaces cannot drift.
uint64_t QueueStatement(const StatementRegistration& reg, std::string* err_out) {
    auto p = std::make_shared<StatementRegistration>(reg);

    kcdx::lua_registry::Entry e;
    e.kind         = kcdx::lua_registry::Kind::Statement;
    e.name         = p->name;
    e.payload      = p;
    e.pluginName   = reg.owningPlugin;
    e.callSiteFile = reg.callSiteFile;
    e.callSiteLine = reg.callSiteLine;

    return kcdx::lua_registry::Append(std::move(e), err_out);
}

// Register the Kind::Statement deferred-apply handler. ENGINE state, not
// Lua-surface state — makes Kind::Statement appliable regardless of which
// surface queued the entry (the C++ kcdxStatementInterface queues at plugin
// Load time, which would hit the same wall the Kind::Hook handler did:
// lua_registry::Append rejects a Kind with no handler, and bind() runs too
// late at first-update-tick). Called at engine init (dllmain.cpp, before
// DiscoverAndLoad).
void RegisterHandlers() {
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Statement, &ApplyStatementEntry);
}

void bind(lua_State* L) {
    kcdx::lua_registry::EnsureHandleMetatable(L);

    // kcdx.statement is a TABLE of sub-verb functions (lua-api-surface.md rule
    // 4a: discrete behavioral variants are sub-verbs, not table keys).
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    const int stmtTbl = lua_gettop(L);

    static const luaL_Reg kVerbs[] = {
        {"replace_with",  Lua_ReplaceWith},
        {"insert_before", Lua_InsertBefore},
        {"insert_after",  Lua_InsertAfter},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* v = kVerbs; v->name; ++v) {
        lua_pushcfunction(L, v->func);
        lua_setfield(L, stmtTbl, v->name);
    }

    lua_setfield(L, kcdx_idx, "statement");
}

}  // namespace kcdx::lua_bind_statement
