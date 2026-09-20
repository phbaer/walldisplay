#!/usr/bin/env python3
"""Validate a factory image before running the physical provisioning checklist.

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

FACTORY_APP_OFFSET = 0x30000


def validate(factory: Path, image: Path | None, manifest: Path | None) -> list[str]:
    if not factory.is_file():
        raise ValueError(f"factory image does not exist: {factory}")

    packaged = image or Path("build/walldisplay.bin")
    if not packaged.is_file():
        raise ValueError(f"final application image does not exist: {packaged}")
    digest = hashlib.sha256(packaged.read_bytes()).hexdigest()
    if factory.stat().st_size < FACTORY_APP_OFFSET + packaged.stat().st_size:
        raise ValueError("factory image is too small to contain the application at 0x30000")
    with factory.open("rb") as stream:
        stream.seek(FACTORY_APP_OFFSET)
        if stream.read(packaged.stat().st_size) != packaged.read_bytes():
            raise ValueError("factory image application does not match the final application image")
    result = [f"factory image: {factory}", f"application SHA-256: {digest}"]
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
    result.append("factory image contents: complete")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factory", type=Path, required=True, help="signed merged factory .bin")
    parser.add_argument("--image", type=Path, help="final signed application image")
    parser.add_argument("--manifest", type=Path, help="matching OTA manifest JSON")
    args = parser.parse_args()
    try:
        print("\n".join(validate(args.factory, args.image, args.manifest)))
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
