#!/usr/bin/env python3
"""Validate a factory package before running the physical provisioning checklist.

This command deliberately does not erase or flash a panel.  It verifies that the
artifact under test is self-contained and that its metadata describes the final
application image.  The destructive hardware steps are documented in
``docs/acceptance/softap-provisioning.md``.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tarfile

REQUIRED_FACTORY_FILES = {
    "walldisplay.bin",
    "bootloader.bin",
    "partition-table.bin",
    "ota_data_initial.bin",
    "partitions.csv",
}


def validate(factory: Path, image: Path | None, manifest: Path | None) -> list[str]:
    if not factory.is_file():
        raise ValueError(f"factory archive does not exist: {factory}")
    with tarfile.open(factory, "r:gz") as archive:
        names = set(archive.getnames())
    missing = REQUIRED_FACTORY_FILES - names
    if missing:
        raise ValueError(f"factory archive is missing: {', '.join(sorted(missing))}")

    packaged = image or Path("build/walldisplay.bin")
    if not packaged.is_file():
        raise ValueError(f"final application image does not exist: {packaged}")
    digest = hashlib.sha256(packaged.read_bytes()).hexdigest()
    result = [f"factory archive: {factory}", f"application SHA-256: {digest}"]
    if manifest:
        try:
            values = json.loads(manifest.read_text())
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid OTA manifest: {error}") from error
        if values.get("sha256") != digest:
            raise ValueError("OTA manifest SHA-256 does not match the final application image")
        if values.get("size") != packaged.stat().st_size:
            raise ValueError("OTA manifest size does not match the final application image")
        if values.get("target") != "esp32s3":
            raise ValueError("OTA manifest target must be esp32s3")
        result.append(f"OTA manifest: {manifest} (hash and size match)")
    result.append("factory archive contents: complete")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factory", type=Path, required=True, help="signed factory .tar.gz")
    parser.add_argument("--image", type=Path, help="final signed application image")
    parser.add_argument("--manifest", type=Path, help="matching OTA manifest JSON")
    args = parser.parse_args()
    try:
        print("\n".join(validate(args.factory, args.image, args.manifest)))
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
