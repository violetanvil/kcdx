"""Build a KCD2 .pak (uncompressed zip) from a staging folder."""
import sys
import zipfile
from pathlib import Path

stage = Path(sys.argv[1])
out_pak = Path(sys.argv[2])

with zipfile.ZipFile(out_pak, "w", compression=zipfile.ZIP_STORED) as z:
    for f in sorted(stage.rglob("*")):
        if f.is_file():
            arcname = f.relative_to(stage).as_posix()
            z.write(f, arcname)
            print(f"  added {arcname} ({f.stat().st_size} bytes)")

print(f"Wrote {out_pak} ({out_pak.stat().st_size} bytes)")
