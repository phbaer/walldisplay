#!/usr/bin/env python3
"""Copy the canonical HA integration into the alternative deployment tree."""
import argparse
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
source = root / "custom_components/walldisplay_sync"
target = root / "config/custom_components/walldisplay_sync"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true", help="Fail if deployable source differs")
args = parser.parse_args()
files = [path for path in source.iterdir() if path.suffix in {".py", ".json"}]
differences = [path for path in files if not (target / path.name).exists() or path.read_bytes() != (target / path.name).read_bytes()]
if args.check:
    for path in differences:
        print(f"Out of sync: {path.name}")
    raise SystemExit(bool(differences))
target.mkdir(parents=True, exist_ok=True)
for path in differences:
    shutil.copyfile(path, target / path.name)
print(f"Updated {len(differences)} deployable integration files")
