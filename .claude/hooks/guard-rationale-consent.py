#!/usr/bin/env python3
# PreToolUse(Edit|Write) — anti-pattern RATIONALE consent gate (repo-specific).
# Forces a user accept-prompt (permissionDecision: "ask") on any write to the
# anti-pattern rationale record. The system guard-anti-pattern-consent.py gates
# .claude/rules/anti-patterns.md; this re-applies the same gate to the rationale
# file, which no system hook covers. Does NOT block: the user clicks allow.
# An agent cannot silently add/change an AP's blessed rationale.
import sys, os, json


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)

    # Gate the rationale file ONLY — never anti-patterns.md (the system guard owns
    # that), never a same-named file elsewhere.
    leaf = os.path.basename(path)
    if leaf != 'anti-pattern-rationale.md':
        sys.exit(0)

    # Only the kcdx governance copy under .claude/, not a same-named file elsewhere.
    norm = path.replace("\\", "/")
    if not norm.endswith('.claude/anti-pattern-rationale.md'):
        sys.exit(0)

    decision = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "ask",
            "permissionDecisionReason": "Editing the anti-pattern rationale record requires user consent — an AP's rationale is a blessed record; confirm you approve this change.",
        }
    }

    print(json.dumps(decision))
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
