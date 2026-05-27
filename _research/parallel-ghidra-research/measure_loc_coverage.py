"""Measure how much of the loc-key set is reachable by the STRING path
(key present as a binary literal) vs the int-ID-only residual.

Gates whether the loc int-ID runtime-dump sub-feature is needed
(parallel-ghidra-research.md §6). User goal: users find what they need.

Usage: python measure_loc_coverage.py <English_xml.pak> <WHGame.dll>
Writes the result to stdout AND to loc-coverage-result.txt beside this script.
"""
import sys, os, zipfile, re

pak, dll = sys.argv[1], sys.argv[2]
out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "loc-coverage-result.txt")
lines = []
def emit(s):
    print(s, flush=True)
    lines.append(s)

# Classify each XML source file as gameplay/UI vs dialogue/content. The file a
# key lives in is a cleaner classifier than prefix-guessing. Content = the lines
# characters speak / quest body text (no gameplay function behind them);
# gameplay = menus/items/HUD/tutorials/misc UI (text a modder ties to behavior).
CONTENT_FILES = {"text_ui_dialog.xml", "text_ui_quest.xml", "text_ui_soul.xml"}

# 1. All loc KEYS (first <Cell> per <Row>), tracked per source file + per class.
keys = set()
keys_by_class = {"gameplay": set(), "content": set()}
z = zipfile.ZipFile(pak)
for n in z.namelist():
    if not n.lower().endswith(".xml"):
        continue
    base = n.split("/")[-1].lower()
    cls = "content" if base in CONTENT_FILES else "gameplay"
    data = z.read(n).decode("utf-8", "replace")
    for m in re.finditer(r"<Row>\s*<Cell>(.*?)</Cell>", data, re.S):
        k = m.group(1).strip()
        if k:
            keys.add(k)
            keys_by_class[cls].add(k)
emit(f"total distinct loc keys (English pak): {len(keys)}")
emit(f"  gameplay/UI keys (menus/items/hud/misc/tutorials/ingame): "
     f"{len(keys_by_class['gameplay'])}")
emit(f"  content keys (dialog/quest/soul): {len(keys_by_class['content'])}")

# 2. All ascii string literals (>=4 chars) in the binary, as a SET (O(1) lookup).
raw = open(dll, "rb").read()
lits = set(m.group().decode("ascii")
           for m in re.finditer(rb"[\x20-\x7e]{4,}", raw))
emit(f"distinct ascii literals (>=4) in WHGame.dll: {len(lits)}")

# 3. Exact-literal coverage (the clean string-path case), overall + per class.
#    This is the number that matters: of GAMEPLAY keys, how many reach a function
#    via the string path vs are int-ID-only.
def cover(kset):
    e = sum(1 for k in kset if k in lits)
    return e, len(kset)

exact = sum(1 for k in keys if k in lits)
emit(f"keys that are EXACT binary literals (all): {exact}  ({100*exact/len(keys):.1f}%)")
for cls in ("gameplay", "content"):
    e, t = cover(keys_by_class[cls])
    emit(f"  {cls} keys as binary literals: {e}/{t}  "
         f"({100*e/t:.1f}%)  -> int-ID-only residual: {t-e} ({100*(t-e)/t:.1f}%)")

# 4. Residual: keys with NO exact literal. These are the int-ID-only candidates
#    (a key not present as a standalone literal can only be reached via the
#    runtime int-ID path, not the string-anchor path).
residual = sorted(k for k in keys if k not in lits)
emit(f"int-ID-only residual (no exact literal): {len(residual)}  "
     f"({100*len(residual)/len(keys):.1f}%)")

# 5. Break the residual down by key prefix family, so we know WHAT loses
#    coverage (dialog/quest vs ui/hud).
fam = {}
for k in residual:
    pre = k.split("_", 1)[0] if "_" in k else k[:8]
    fam[pre] = fam.get(pre, 0) + 1
emit("residual by key-prefix family (top 15):")
for pre, c in sorted(fam.items(), key=lambda kv: -kv[1])[:15]:
    emit(f"    {pre:<20} {c}")
emit("sample residual keys:")
for k in residual[:15]:
    emit(f"    {k}")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
emit(f"\n(written to {out_path})")
