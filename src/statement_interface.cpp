// kcdx::statement_interface — engine-side impl of kcdxStatementInterface.
//
// Mirrors the Lua kcdx.statement.* binder (src/lua_bind_statement.cpp) — the
// SAME registration intent, queued through the SAME seam
// (lua_bind_statement::QueueStatement → a Kind::Statement deferred-apply
// entry), applied by the SAME apply handler in unified load order. This file
// owns ONLY the C-ABI edge: argument validation with teaching errors, the
// kcdxOp → OpView classification call (lua_bind_op::BuildOpView — the one
// shared per-op table), the kcdxLocator → refdb::StatementLocator field
// mapping (this file is that conversion's one home — ConvertLocator, exposed
// through statement_interface.h and shared with the hook interface's insert
// thunks), and the target-name resolution (positional string, or the
// opts->targetRef reference which wins when set; the ref-collapse semantics
// are the shared CollapseTargetRef, also exposed through the header). No
// queue/validation/emit logic is duplicated here.
//
// The insert thunks mirror the register-and-DEFER contract: the statement-
// locator capture-thunk apply path is unwired on BOTH surfaces, so an insert
// registers (a non-zero handle) and the apply pass fails it LOUD with the
// teaching reason — IsApplied(h) false, GetReason(h) non-empty. The callback
// pointer is deliberately NOT stored (the apply path that would invoke it does
// not exist yet; storing it would hold a pointer nothing ever consumes —
// mirroring the Lua verb, which defers taking its callback ref for the same
// reason). When the capture-thunk path lands, the thunks start carrying the
// callback and the apply handler installs the insert.
//
// Query thunks (IsApplied / GetReason / GetName) walk the registry by handle
// id — the SAME Find() the Lua handle metatable methods and hook_interface's
// query thunks use (one handle space). Uninstall is honestly NOT supported for
// statement handles (no byte-revert path exists for an applied statement
// rewrite): it returns false + logs the teaching reason, mirroring the Lua
// statement handle's :uninstall() teaching error — never a silently-flipped
// status over live bytes.

#include "statement_interface.h"

#include <string>

#include "log.h"
#include "lua_bind_op.h"         // BuildOpView — the shared op classification
#include "lua_bind_statement.h"  // StatementRegistration + QueueStatement (the shared queue seam)
#include "lua_registry.h"        // Find / Status — the shared handle space
#include "plugin_loader.h"       // NameForHandle / AuthorForHandle
#include "refdb.h"               // refdb::StatementLocator

namespace kcdx::statement_interface {

// kcdxLocator → refdb::StatementLocator — the C-ABI locator's ONE conversion
// home (declared in statement_interface.h; shared with the hook interface's
// insert thunks). A pure field mapping: each kind copies its own operand(s);
// a null C-string maps to the empty/unset form; a non-null Matching key sets
// its has_ flag (null = key not provided, the C-ABI spelling of the Lua
// matching{} table's absent key). Returns false + a teaching reason on an
// unrecognized kind value or a missing required operand — fail loud, never a
// silently-defaulted locator.
bool ConvertLocator(const kcdxLocator& in, refdb::StatementLocator& out,
                    std::string& err) {
    auto required = [&](const char* s, const char* kindName,
                        const char* field) -> bool {
        if (s && s[0]) return true;
        err = std::string(kindName) + " requires locator." + field +
              " (a non-empty string) — the operand that names what the "
              "locator matches. See the kcdxLocator field contract in "
              "kcdx/Interfaces.h.";
        return false;
    };

    switch (in.kind) {
        case kcdxLocator_FunctionEntry:
            out.kind = refdb::StatementLocatorKind::FunctionEntry;
            return true;
        case kcdxLocator_FunctionExit:
            out.kind = refdb::StatementLocatorKind::FunctionExit;
            return true;
        case kcdxLocator_FirstCallTo:
            if (!required(in.calleeOrFn, "kcdxLocator_FirstCallTo", "calleeOrFn"))
                return false;
            out.kind = refdb::StatementLocatorKind::FirstCallTo;
            out.callee_or_fn = in.calleeOrFn;
            return true;
        case kcdxLocator_LastCallTo:
            if (!required(in.calleeOrFn, "kcdxLocator_LastCallTo", "calleeOrFn"))
                return false;
            out.kind = refdb::StatementLocatorKind::LastCallTo;
            out.callee_or_fn = in.calleeOrFn;
            return true;
        case kcdxLocator_CallTo:
            if (!required(in.calleeOrFn, "kcdxLocator_CallTo", "calleeOrFn"))
                return false;
            out.kind = refdb::StatementLocatorKind::CallTo;
            out.callee_or_fn = in.calleeOrFn;
            return true;
        case kcdxLocator_FirstReturn:
            out.kind = refdb::StatementLocatorKind::FirstReturn;
            return true;
        case kcdxLocator_LastReturn:
            out.kind = refdb::StatementLocatorKind::LastReturn;
            return true;
        case kcdxLocator_ReturnValue:
            if (!required(in.returnValueOperand, "kcdxLocator_ReturnValue",
                          "returnValueOperand"))
                return false;
            out.kind = refdb::StatementLocatorKind::ReturnValue;
            out.return_value_operand = in.returnValueOperand;
            return true;
        case kcdxLocator_ReferencesString:
            if (!required(in.stringArg, "kcdxLocator_ReferencesString",
                          "stringArg"))
                return false;
            out.kind = refdb::StatementLocatorKind::ReferencesString;
            out.string_arg = in.stringArg;
            return true;
        case kcdxLocator_FirstReadOfCvar:
            if (!required(in.stringArg, "kcdxLocator_FirstReadOfCvar",
                          "stringArg"))
                return false;
            out.kind = refdb::StatementLocatorKind::FirstReadOfCvar;
            out.string_arg = in.stringArg;
            return true;
        case kcdxLocator_Matching:
            out.kind = refdb::StatementLocatorKind::Matching;
            if (in.matchKind && in.matchKind[0]) {
                out.has_match_kind = true;
                out.match_kind = in.matchKind;
            }
            if (in.matchCallee && in.matchCallee[0]) {
                out.has_match_callee = true;
                out.match_callee = in.matchCallee;
            }
            if (in.matchConditionContains && in.matchConditionContains[0]) {
                out.has_match_condition_contains = true;
                out.match_condition_contains = in.matchConditionContains;
            }
            if (in.matchReadsCvar && in.matchReadsCvar[0]) {
                out.has_match_reads_cvar = true;
                out.match_reads_cvar = in.matchReadsCvar;
            }
            if (in.matchReferencesString && in.matchReferencesString[0]) {
                out.has_match_references_string = true;
                out.match_references_string = in.matchReferencesString;
            }
            return true;
        case kcdxLocator_MatchingPattern:
            if (!required(in.aobPattern, "kcdxLocator_MatchingPattern",
                          "aobPattern"))
                return false;
            out.kind = refdb::StatementLocatorKind::MatchingPattern;
            out.aob_pattern = in.aobPattern;
            return true;
    }
    err = "locator.kind " + std::to_string(static_cast<int>(in.kind)) +
          " is not a recognized kcdxLocatorKind value — use one of the "
          "kcdxLocator_* catalog values. The catalog is append-only; an "
          "unknown value usually means a header/engine version mismatch.";
    return false;
}

// Collapse a target-by-reference kcdxFunctionRef to its carried name —
// declared in statement_interface.h, the ONE home of the ref-as-target
// semantics shared by the statement verbs' opts->targetRef AND the hook
// verbs' opts->targetRef (src/hook_interface.cpp). The reference collapses to
// its carried name (the same behavior the Lua verbs' reference-value target
// path has); a not-found or nameless reference fails loud, never a silent
// fallback.
bool CollapseTargetRef(const kcdxFunctionRef& ref, std::string& nameOut,
                       std::string& err) {
    if (!ref.found) {
        err = std::string("opts.targetRef carries found=false (reason \"") +
              (ref.reason ? ref.reason : "") +
              "\") — a reference that did not resolve cannot name a "
              "target. Mint it via kcdxFunctionsInterface and check "
              "ref.found before passing it.";
        return false;
    }
    if (!ref.name || !ref.name[0]) {
        err = "opts.targetRef carries no resolvable name (a GameById "
              "reference — the id is its handle, not a name). The verb "
              "resolves its target by the curated function NAME: pass a "
              "GameByName / PluginByName reference, or the name string as "
              "`target`.";
        return false;
    }
    nameOut = ref.name;
    return true;
}

namespace {

// Push a teaching error to the engine log + the calling plugin's log (the
// auto-loud-on-failure contract every registration method documents). The
// caller's plugin handle drives the per-plugin routing; kcdxInvalidPluginHandle
// falls back to engine-only. Same shape as the hook interface's teaching-error
// helper so the two surfaces grep alike.
void LogTeachingError(kcdxPluginHandle owningPlugin,
                      const char* verb,
                      const std::string& msg) {
    LOG_ERROR_KV("STATEMENT_INTERFACE", "register_failed",
        ::kcdx::log::KV("verb",   verb),
        ::kcdx::log::KV("plugin", kcdx::plugins::NameForHandle(owningPlugin).c_str()),
        ::kcdx::log::KV("reason", msg.c_str()));
    if (owningPlugin != kcdxInvalidPluginHandle) {
        LOG_PLUGIN_ERROR(owningPlugin, "STATEMENT_INTERFACE",
            "kcdxStatementInterface::%s — %s", verb, msg.c_str());
    }
}

// Resolve the registration's target NAME: opts->targetRef WINS when set (the
// resolve-once-pass-to-N-verbs affordance — the reference collapses to its
// carried name via the shared CollapseTargetRef, the same semantics the hook
// verbs' opts->targetRef uses); otherwise the positional `target` string is
// used (the common path). Returns false + a teaching reason on a not-found /
// nameless reference or a missing target — fail loud, never a silent
// fallback.
bool ResolveTargetName(const char* target, const kcdxStatementOptions* opts,
                       std::string& nameOut, std::string& err) {
    if (opts && opts->targetRef) {
        return CollapseTargetRef(*opts->targetRef, nameOut, err);
    }
    if (target && target[0]) {
        nameOut = target;
        return true;
    }
    err = "specify `target` = the curated function NAME the statement lives "
          "in (the engine resolves the statement, the bytes, and the fit for "
          "you), or set opts.targetRef to a kcdxFunctionRef minted by "
          "kcdxFunctionsInterface.";
    return false;
}

// Thread the shared opts → registration fields (identity, module default,
// owner attribution) common to all three registration thunks.
void ThreadOpts(const std::string& targetName,
                const kcdxStatementOptions* opts,
                kcdx::lua_bind_statement::StatementRegistration& reg) {
    if (opts && opts->name && opts->name[0]) {
        reg.name = opts->name;
    } else {
        // Engine-synthesized default, tagged with the target so a no-name
        // registration is still greppable (the same <cppsynth> discipline the
        // hook interface uses).
        reg.name = std::string("<cppsynth>:") + targetName;
    }
    if (opts && opts->description && opts->description[0]) {
        reg.description = opts->description;
    }
    reg.module = (opts && opts->module && opts->module[0])
                     ? opts->module
                     : "WHGame.dll";  // the engine-substituted default, same
                                      // as kcdxHookOptions.module.

    const kcdxPluginHandle owning =
        opts ? opts->owningPlugin : kcdxInvalidPluginHandle;
    reg.owningAuthor = kcdx::plugins::AuthorForHandle(owning);
    reg.owningPlugin = kcdx::plugins::NameForHandle(owning);
    // No script call site on the C++ surface (a compiled DLL).
    reg.callSiteFile.clear();
    reg.callSiteLine = 0;
}

// -----------------------------------------------------------------------
// Registration thunks (3) — one per kcdxStatementInterface method.
// -----------------------------------------------------------------------

kcdxStatementHandle Thunk_ReplaceWith(const char* target, const kcdxOp* op,
                                      const kcdxStatementOptions* opts) {
    const kcdxPluginHandle owning =
        opts ? opts->owningPlugin : kcdxInvalidPluginHandle;
    const char* verb = "ReplaceWith";

    if (!op) {
        LogTeachingError(owning, verb,
            "op is null — ReplaceWith takes a STATIC op (a kcdxOp value, e.g. "
            "kcdxOp op = { kcdxOp_ReplaceWithNoop };), never a callback. A "
            "per-call callback is kcdxHookInterface's job; kcdx.statement is "
            "zero-per-call static bytes.");
        return 0;
    }

    std::string targetName, err;
    if (!ResolveTargetName(target, opts, targetName, err)) {
        LogTeachingError(owning, verb, err);
        return 0;
    }

    // Classify the op through the SAME per-op table the Lua constructors use
    // (required statement kind, determinate-vs-deferred emit, operand gating).
    kcdx::lua_bind_op::OpView view;
    if (!kcdx::lua_bind_op::BuildOpView(op->kind, op->value, op->targetFn,
                                        view, err)) {
        LogTeachingError(owning, verb, err);
        return 0;
    }

    // Optional locator (ReplaceWith only); null = the function's first
    // statement (the function_entry default, mirroring the Lua verb).
    refdb::StatementLocator locator;
    if (opts && opts->locator) {
        if (!ConvertLocator(*opts->locator, locator, err)) {
            LogTeachingError(owning, verb, err);
            return 0;
        }
    }

    kcdx::lua_bind_statement::StatementRegistration reg;
    reg.targetName = targetName;
    reg.locator    = locator;
    reg.op         = view;
    reg.hasOp      = true;
    ThreadOpts(targetName, opts, reg);

    uint64_t handleId = kcdx::lua_bind_statement::QueueStatement(reg, &err);
    if (handleId == 0) {
        LogTeachingError(owning, verb, err);
    }
    return handleId;
}

// Shared body for InsertBefore / InsertAfter — identical registration shape;
// the before/after distinction becomes meaningful when the capture-thunk apply
// path lands (today both register-and-defer identically, the same as the two
// Lua insert verbs).
kcdxStatementHandle InstallInsert(const char* target,
                                  const kcdxLocator* locator,
                                  void* callback,
                                  const kcdxStatementOptions* opts,
                                  const char* verb) {
    const kcdxPluginHandle owning =
        opts ? opts->owningPlugin : kcdxInvalidPluginHandle;

    if (!locator) {
        LogTeachingError(owning, verb,
            "locator is null — the locator is REQUIRED on an insert (\"insert "
            "before what?\" has no default). Pass a kcdxLocator naming the "
            "statement, e.g. kcdxLocator loc = { kcdxLocator_FirstCallTo }; "
            "loc.calleeOrFn = \"IsInCombat\";");
        return 0;
    }
    if (!callback) {
        LogTeachingError(owning, verb,
            "callback is null — the insert methods are callback-form (a "
            "function pointer cast to void*). For a STATIC change use "
            "ReplaceWith with a kcdxOp value instead.");
        return 0;
    }

    std::string targetName, err;
    if (!ResolveTargetName(target, opts, targetName, err)) {
        LogTeachingError(owning, verb, err);
        return 0;
    }

    refdb::StatementLocator loc;
    if (!ConvertLocator(*locator, loc, err)) {
        LogTeachingError(owning, verb, err);
        return 0;
    }

    // Register-and-defer: the entry carries insertPending and the apply pass
    // fails it LOUD with the not-yet-wired teaching reason (IsApplied false,
    // GetReason non-empty). The callback pointer is NOT stored — the apply
    // path that would invoke it is unwired, and holding a pointer nothing
    // consumes would claim more than the engine delivers (the Lua verb defers
    // taking its callback ref the same way).
    kcdx::lua_bind_statement::StatementRegistration reg;
    reg.targetName    = targetName;
    reg.locator       = loc;
    reg.insertPending = true;
    ThreadOpts(targetName, opts, reg);

    uint64_t handleId = kcdx::lua_bind_statement::QueueStatement(reg, &err);
    if (handleId == 0) {
        LogTeachingError(owning, verb, err);
    }
    return handleId;
}

kcdxStatementHandle Thunk_InsertBefore(const char* target,
                                       const kcdxLocator* locator,
                                       void* callback,
                                       const kcdxStatementOptions* opts) {
    return InstallInsert(target, locator, callback, opts, "InsertBefore");
}

kcdxStatementHandle Thunk_InsertAfter(const char* target,
                                      const kcdxLocator* locator,
                                      void* callback,
                                      const kcdxStatementOptions* opts) {
    return InstallInsert(target, locator, callback, opts, "InsertAfter");
}

// -----------------------------------------------------------------------
// Query thunks (4) — walk the SAME registry handle space the Lua handle
// userdata wraps (lua_registry::Find / Status), the same shape as the hook
// interface's query thunks. handleId 0 / unknown entries flow through the
// documented sentinel returns.
// -----------------------------------------------------------------------

bool Thunk_IsApplied(kcdxStatementHandle h) {
    if (h == 0) return false;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return false;
    return e->status.load(std::memory_order_acquire) ==
           kcdx::lua_registry::Status::Applied;
}

const char* Thunk_GetReason(kcdxStatementHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    // Null when the handle is valid AND applied; otherwise the stored
    // teaching reason. Find() returns a pointer into the node-stable entry
    // deque, so the c_str() lifetime equals the registry entry's lifetime
    // (process lifetime; entries are append-only).
    if (e->status.load(std::memory_order_acquire) ==
        kcdx::lua_registry::Status::Applied) {
        return nullptr;
    }
    if (e->reason.empty()) return nullptr;
    return e->reason.c_str();
}

const char* Thunk_GetName(kcdxStatementHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    return e->name.c_str();
}

bool Thunk_Uninstall(kcdxStatementHandle h) {
    // Honestly unsupported: an applied statement rewrite has no byte-revert
    // path (no original-bytes snapshot is stored), so there is nothing safe
    // to revert to. Return false + the teaching reason — never a silently-
    // flipped "removed" status over bytes that are still live (that would
    // make IsApplied lie). Mirrors the Lua statement handle's :uninstall()
    // teaching error; a per-kind uninstall (snapshot + revert) ships as its
    // own later feature.
    LOG_WARN_KV("STATEMENT_INTERFACE", "uninstall_unsupported",
        ::kcdx::log::KV("handle", static_cast<long long>(h)),
        ::kcdx::log::KV::BareStr("detail",
            "Uninstall is not yet supported for statement handles — the "
            "engine stores no original-bytes snapshot to revert an applied "
            "statement rewrite. The registration's status is unchanged."));
    return false;
}

// -----------------------------------------------------------------------
// Vtable instance. Order MATCHES the kcdxStatementInterface struct field
// order in include/kcdx/Interfaces.h byte-for-byte (append-only ABI; fixed
// offsets). DO NOT reorder.
// -----------------------------------------------------------------------

kcdxStatementInterface g_statementInterface = {
    /*ReplaceWith=*/  Thunk_ReplaceWith,
    /*InsertBefore=*/ Thunk_InsertBefore,
    /*InsertAfter=*/  Thunk_InsertAfter,
    /*IsApplied=*/    Thunk_IsApplied,
    /*GetReason=*/    Thunk_GetReason,
    /*GetName=*/      Thunk_GetName,
    /*Uninstall=*/    Thunk_Uninstall,
};

}  // namespace

const kcdxStatementInterface* GetInterface() {
    return &g_statementInterface;
}

}  // namespace kcdx::statement_interface
