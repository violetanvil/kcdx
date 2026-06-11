#!/usr/bin/env python3
# UserPromptSubmit — per-turn cornerstone-gate reminder injection.
# Prints one line to stdout (exit 0 -> injected as context adjacent to the
# user's message every turn). Prevention layer for the cornerstone gate:
# the agent sees the obligation BEFORE forming options, regardless of which
# skill (or none) is running and regardless of context length. The catch
# layer is guard-cornerstone-gate.py; the canon is
# .claude/rules/cornerstones.md §"Surfaced options clear the cornerstones first".
import sys

def main():
    try:
        sys.stdin.read()
    except Exception:
        pass
    sys.stdout.write(
        "[kcdx cornerstone gate] Surfacing options / a design fork / a recommendation "
        "this turn? Run each option past .claude/rules/cornerstones.md FIRST - each "
        "option states its cornerstone standing (+ the disassembler-test verdict where "
        "author-facing) or that no cornerstone bears; the Recommendation names the "
        "cornerstone it wins on."
    )
    sys.exit(0)

if __name__ == "__main__":
    main()
