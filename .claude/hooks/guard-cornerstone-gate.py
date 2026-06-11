#!/usr/bin/env python3
# PreToolUse(AskUserQuestion) — cornerstone-gate backstop. BLOCKS (exit 2,
# block-and-retry: a pause, not a veto) an option set presented with no
# evidence the cornerstone screen ran. Evidence = the token "cornerstone"
# anywhere in the question payload (a per-option standing, or the explicit
# "no cornerstone bears on this fork" neutral escape). Prevention layer is
# inject-cornerstone-reminder.py; canon is .claude/rules/cornerstones.md
# §"Surfaced options clear the cornerstones first". Compliant calls pass
# silently.
import sys, json

def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        sys.exit(0)
    ti = data.get("tool_input") or {}
    questions = ti.get("questions")
    if not questions:
        sys.exit(0)
    payload = json.dumps(questions).lower()
    if "cornerstone" in payload:
        sys.exit(0)
    sys.stderr.write(
        "Cornerstone-gate BLOCK: these options carry no cornerstone screening evidence. "
        "Run every option past .claude/rules/cornerstones.md (UX > Capability > Performance, "
        "never traded for effort; the disassembler test on author-facing surfaces), then "
        "re-issue this question with each option's cornerstone standing in its description "
        "(+ the disassembler-test verdict where author-facing) and the Recommendation naming "
        "the cornerstone it wins on - or state explicitly that no cornerstone bears on this "
        "fork. A pause, not a veto: screen, then re-ask."
    )
    sys.exit(2)

if __name__ == "__main__":
    main()
