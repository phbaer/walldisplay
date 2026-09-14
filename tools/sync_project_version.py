#!/usr/bin/env python3
"""Synchronize generated project-version references from pyproject.toml.

Generated references are committed because the firmware header, HA manifest,
and blueprint must remain valid when the repository is used outside CI.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import tomllib


ROOT = Path(__file__).resolve().parents[1]


def project_version(root: Path = ROOT) -> str:
    """Read the canonical project version."""
    with (root / "pyproject.toml").open("rb") as source:
        project = tomllib.load(source).get("project", {})
    version = project.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("pyproject.toml must define a numeric MAJOR.MINOR.PATCH project version")
    return version


def _replace(path: Path, pattern: str, version: str, *, flags: int = 0) -> str:
    text = path.read_text()
    updated, count = re.subn(pattern, rf"\g<prefix>{version}\g<suffix>", text, count=1, flags=flags)
    if count != 1:
        raise ValueError(f"Expected one version reference in {path}")
    return updated


def synchronize(root: Path = ROOT, *, check: bool = False) -> list[Path]:
    """Update all checked-in consumers and return files that changed."""
    version = project_version(root)
    references = [
        (root / "include/walldisplay/app_config.h", r"(?P<prefix>#define APP_FW_VERSION\s+\")(?:[^\"]+)(?P<suffix>\")"),
        (root / "custom_components/walldisplay_sync/manifest.json", r'(?P<prefix>"version"\s*:\s*")(?:[^\"]+)(?P<suffix>")'),
        (root / "config/custom_components/walldisplay_sync/manifest.json", r'(?P<prefix>"version"\s*:\s*")(?:[^\"]+)(?P<suffix>")'),
        (root / "custom_components/walldisplay_sync/__init__.py", r'(?P<prefix>"version"\s*:\s*")(?:[^\"]+)(?P<suffix>")'),
        (root / "config/custom_components/walldisplay_sync/__init__.py", r'(?P<prefix>"version"\s*:\s*")(?:[^\"]+)(?P<suffix>")'),
        (root / "config/blueprints/automation/walldisplay/mqtt_sync.yaml", r"(?P<prefix>  name: WallDisplay MQTT Sync v)[0-9]+\.[0-9]+\.[0-9]+(?P<suffix>[^\n]*)"),
        (root / "config/blueprints/automation/walldisplay/mqtt_sync.yaml", r'(?P<prefix>  blueprint_version:\s*")(?:[^\"]+)(?P<suffix>")'),
    ]
    changed: list[Path] = []
    for path, pattern in references:
        original = path.read_text()
        updated = _replace(path, pattern, version, flags=re.MULTILINE)
        if updated == original:
            continue
        if check:
            if path not in changed:
                changed.append(path)
        else:
            path.write_text(updated)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if generated references differ")
    args = parser.parse_args()
    changed = synchronize(check=args.check)
    if args.check and changed:
        names = ", ".join(str(path.relative_to(ROOT)) for path in changed)
        raise SystemExit(f"Project version references are out of date: {names}; run tools/sync_project_version.py")
    if not args.check:
        print(f"Synchronized project version {project_version()}")


if __name__ == "__main__":
    main()
