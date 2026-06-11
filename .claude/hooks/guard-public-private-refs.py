#!/usr/bin/env python3
# PreToolUse(Write|Edit) — public/private boundary guard.
# WARN-ONLY: never blocks. Flags a Write/Edit to a PUBLIC-FACING file that
# introduces a reference to a PRIVATE document or the AI-development vocabulary.
# A public file ships to the public remote (publish-public allowlist); it
# must not reference anything that stays private — the reference would be a
# broken link on public and a trace of how the repo is built.
import sys, os, re, json, subprocess


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)

    # Normalize to a repo-relative, forward-slash path.
    repo_root = None
    try:
        r = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True)
        if r.returncode == 0:
            repo_root = r.stdout.strip()
    except Exception:
        repo_root = None
    rel = path.replace("\\", "/")
    if repo_root:
        root_fwd = repo_root.replace("\\", "/").rstrip("/")
        if rel.startswith(root_fwd):
            rel = rel[len(root_fwd):].lstrip("/")

    # PUBLIC-FACING = under an allowlisted public dir, or an allowlisted root file.
    # Keep in sync with publish-public's allowlist.
    public_dirs = ['src/', 'include/', 'vendor/', 'data/', 'examples/',
                   'kcdx-engine/', 'test-plugins/', 'tools/', 'docs/']
    public_files = ['README.md', 'LICENSE', 'CMakeLists.txt', 'build.ps1',
                    'package-release.ps1']
    # Carve-outs: private subpaths inside an otherwise-public dir (internal
    # planning + bug trails under docs/). Keep in sync with publish-public
    # $PrivateSubpaths. Trailing '/' = directory prefix; else an exact-file carve-out.
    private_subpaths = ['docs/outstanding-work/', 'docs/known-issues/',
                        'docs/tech-debt/', 'docs/design/', 'docs/design.md',
                        'docs/design-gaps.md',
                        'docs/phase5c7b-plan.md', 'docs/VERIFY_PHASE2.md',
                        'docs/VERIFY_PHASE3.md', 'docs/VERIFY_PHASE4.md',
                        'docs/archive/', 'docs/phase5-rom-port-plan.md',
                        'docs/migration.md', 'examples/archive/',
                        'data/refdata-extractor/', 'data/db-export/',
                        'data/db-export-bulk/', 'data/maintainer-tool/']

    is_public = False
    for d in public_dirs:
        if rel.startswith(d):
            is_public = True
            break
    if not is_public:
        for f in public_files:
            if rel == f:
                is_public = True
                break
    if is_public:
        for p in private_subpaths:
            if p.endswith('/'):
                if rel.startswith(p):
                    is_public = False
                    break
            elif rel == p:
                is_public = False
                break
    if not is_public:
        sys.exit(0)   # private file — may reference anything.

    # Post-operation content (Write supplies it; Edit applies the replacement).
    content = None
    if ti.get("content"):
        content = ti.get("content")
    elif ti.get("old_string"):
        if not os.path.isfile(path):
            sys.exit(0)
        with open(path, "r", encoding="utf-8") as fh:
            current = fh.read()
        old = ti.get("old_string")
        new = ti.get("new_string") or ""
        if ti.get("replace_all"):
            content = current.replace(old, new)
        else:
            idx = current.find(old)
            if idx < 0:
                sys.exit(0)
            content = current[:idx] + new + current[idx + len(old):]
    if not content:
        sys.exit(0)

    # --- Forbidden references in a public file -------------------------------
    # 1) Literal private paths (these dirs/files never reach public).
    # 2) AI-development vocabulary (would betray how the repo is built).
    # loader-architecture.md is EXCLUDED from the private rule-file-name list —
    # it collides with the public doc docs/loader-architecture.md (a bare ref is
    # ambiguous; the .claude/ path form is still caught above). All others are
    # private-only basenames.
    patterns = [
        ('.claude/ path reference', re.compile(r'\.claude/')),
        ('CLAUDE.md reference', re.compile(r'CLAUDE\.md')),
        ('_research/ path reference', re.compile(r'_research/')),
        ('third-party-ghidra/ ref', re.compile(r'third-party-ghidra/')),
        ('test-fixtures/ reference', re.compile(r'test-fixtures/')),
        ('publish-public.ps1 ref', re.compile(r'publish-public\.ps1')),
        ('Claude / Anthropic', re.compile(r'\b(claude|anthropic)\b', re.I)),
        ('subagent / orchestrator', re.compile(r'\b(subagent|orchestrator)\b', re.I)),
        ('AP-rule citation', re.compile(r'\bAP\d{1,2}\b')),
        ('skill invocation', re.compile(r'(?<![A-Za-z0-9_])/(execute|feature|debug|commit|code-review|verification-checkpoint|research-disassembly|governance-architect|senior-architect-(consult|reply)|step-review|architect-review)\b')),
        ('PROBE-naming scheme', re.compile(r'\bPROBE [A-Z](\.\d+)*\b')),
        ('dev-phase scheme', re.compile(r'\bPhase \d+[a-z]?(\.\d+)*\b')),
        ('dev-subphase scheme', re.compile(r'\bsub-\d+[a-z]?\b')),
        ('FIX-naming scheme', re.compile(r'\bFIX [A-Z]\b')),
        ('private rule-file name', re.compile(r'\b(cornerstones|anti-patterns|skse-parity|toml-schema|hook-engine|concurrency-git|results-driven|address-library|lua-bridge|lua-api-surface|naming-namespaces|docs-discipline|deletion-hygiene|lua-precision|lua-callback-threading|reverse-engineering|pak-mods|test-suite|skeptical-expert|public-private-boundary|fail-state-logging|anti-pattern-rationale)\.md\b')),
    ]

    hits = []
    lines = content.split("\n")
    for i, line in enumerate(lines):
        for label, rx in patterns:
            if rx.search(line):
                snippet = line.strip()
                if len(snippet) > 100:
                    snippet = snippet[:100] + '...'
                hits.append("  line {0}: {1} -> {2}".format(i + 1, label, snippet))

    if hits:
        msg = ("Public/private boundary WARN: {0} is a PUBLIC-facing file but references private material or AI-development vocabulary. ".format(rel) +
               "Public files ship to the public remote and must reference NONE of: .claude/, CLAUDE.md, _research/, third-party-ghidra/, test-fixtures/, publish-public.ps1, or the words Claude/Anthropic/subagent/orchestrator/AP<n>/skill-slash-commands. " +
               "Rewrite to state the fact directly without the private citation. Findings:\n" +
               "\n".join(hits))
        sys.stderr.write(msg + "\n")
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
