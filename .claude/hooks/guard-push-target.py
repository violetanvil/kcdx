#!/usr/bin/env python3
# PreToolUse(Bash|PowerShell) — public-leak / remote-identity guard
# (.claude/rules/concurrency-git.md §"Remotes"; CLAUDE.md publish-public rule).
# The private tree is TRACKED, not gitignored — a hand `git push public` ships
# .claude/, _research/, CLAUDE.md, the whole AI-dev trace. Irreversible once
# pushed. Public is reached ONLY via publish-public.ps1 (allowlist projection).
#
# BLOCK: `git push public ...`, `git push --all`, `git push --mirror` — each ships
#        the comprehensive tree to a remote it must never reach by hand.
# WARN:  `git remote add|set-url`, `git config user.email|user.name` — silent
#        identity/remote drift that a later push could leak through.
# Untouched: bare `git push` (-> private), publish-public.ps1, push to origin/private.
import sys, re, json


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    cmd = ti.get("command")
    if not cmd:
        sys.exit(0)

    cp = r'(?:^|[;&|]|&&|\|\|)\s*'
    # Only inspect actual `git push` invocations at command position.
    is_push = re.search(r'(?i)' + cp + r'git\s+push\b', cmd) is not None

    if is_push:
        # --all / --mirror push the comprehensive ref set regardless of remote.
        if re.search(r'(?i)\s--(?:all|mirror)\b', cmd):
            sys.stderr.write("Push-target BLOCK: `git push --all` / `--mirror` ships EVERY ref of the comprehensive private tree (.claude/, _research/, CLAUDE.md — all TRACKED, not gitignored) and can reach the public remote. The private->public boundary is the publish-public.ps1 allowlist, NEVER a hand push (.claude/rules/concurrency-git.md §Remotes). Push to private explicitly (bare `git push`), or run publish-public.ps1 for a sanitized public snapshot.")
            sys.exit(2)
        # An explicit `public` remote arg. Isolate the push's own segment so a
        # `public` inside a later chained command / quoted message can't false-trip.
        seg = cmd
        m = re.search(r'(?i)' + cp + r'git\s+push\b[^\n;&|]*', cmd)
        if m:
            seg = m.group(0)
        if re.search(r'(?i)\bpush\b[^\n]*\bpublic\b', seg):
            sys.stderr.write("Push-target BLOCK: a hand `git push public` ships the comprehensive private tree (.claude/, _research/, CLAUDE.md, the AI-dev trace — TRACKED, not gitignored) to the PUBLIC remote. This is a one-way, irreversible leak. The public remote is updated ONLY by publish-public.ps1, which projects an explicit allowlist (.claude/rules/concurrency-git.md §Remotes; CLAUDE.md). Run that script for a public update; never push public by hand.")
            sys.exit(2)
        sys.exit(0)

    # Remote / identity mutation — WARN (silent drift a future push could leak).
    warn_rules = [
        (re.compile(r'(?i)' + cp + r'git\s+remote\s+(?:add|set-url)\b'),
         "`git remote add/set-url` changes where a push goes — a mis-set remote could route a later push to public (.claude/rules/concurrency-git.md §Remotes)"),
        (re.compile(r'(?i)' + cp + r'git\s+config\s+(?:--\w+\s+)*user\.(?:email|name)\b'),
         "`git config user.email/name` changes commit identity — drift here puts the wrong author on commits (workspace suppresses attribution by policy)"),
    ]
    for rx, why in warn_rules:
        if rx.search(cmd):
            sys.stderr.write("Remote/identity WARN: " + why + ". Confirm this is intended; the private/public split and commit identity are project invariants. This does not block.")
            sys.exit(0)
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
