#!/usr/bin/env python3
"""Enumerate embedded CryEngine/Game source-subsystem dirs in WHGame.dll.

Read-only ground-truth probe: which subsystem source trees are statically
linked (each embedded .cpp path names its subsystem dir). Tells front-2 which
entity/inventory/interaction subsystems exist as code, before any caller-walk.
"""
import re
from find_strings import extract, DLL  # reuse the extractor

S = extract(DLL)
paths = sorted({s for s in S if s.lower().endswith(".cpp") and ("\\" in s or "/" in s)})
subsys = {}
files_by_sub = {}
for p in paths:
    m = re.search(r"[Cc]ode[\\/]([A-Za-z0-9]+)[\\/]([A-Za-z0-9]+)", p)
    if m:
        key = m.group(1) + "/" + m.group(2)
        subsys[key] = subsys.get(key, 0) + 1
        files_by_sub.setdefault(key, []).append(p.rsplit("\\", 1)[-1].rsplit("/", 1)[-1])

print(f"# total .cpp paths: {len(paths)}")
print("=== subsystem dirs (count) ===")
for k, v in sorted(subsys.items(), key=lambda kv: -kv[1]):
    print(f"  {v:4d}  {k}")

# Highlight the event-relevant subsystems and list their files
print("\n=== files in event-relevant subsystem dirs ===")
for key in sorted(files_by_sub):
    if re.search(r"Entity|Inventory|Action|Game|RPG|Item|Interact|Level|AISystem|MovementSystem", key, re.I):
        print(f"\n[{key}]")
        for f in sorted(set(files_by_sub[key])):
            print("   " + f)
