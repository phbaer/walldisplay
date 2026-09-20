import hashlib
import json
import tarfile
from pathlib import Path

import pytest

from tools.acceptance_check import validate


def _archive(path: Path):
    with tarfile.open(path, "w:gz") as archive:
        for name in ("walldisplay.bin", "bootloader.bin", "partition-table.bin", "ota_data_initial.bin", "partitions.csv"):
            source = path.parent / name
            source.write_bytes(b"fixture")
            archive.add(source, arcname=name)


def test_acceptance_check_validates_manifest(tmp_path):
    factory = tmp_path / "factory.tar.gz"
    _archive(factory)
    image = tmp_path / "image.bin"
    image.write_bytes(b"signed image")
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({"target": "esp32s3", "sha256": hashlib.sha256(image.read_bytes()).hexdigest(), "size": image.stat().st_size}))
    assert "contents: complete" in "\n".join(validate(factory, image, manifest))


def test_acceptance_check_rejects_incomplete_archive(tmp_path):
    factory = tmp_path / "factory.tar.gz"
    with tarfile.open(factory, "w:gz"):
        pass
    with pytest.raises(ValueError, match="missing"):
        validate(factory, tmp_path / "image.bin", None)


def test_acceptance_check_rejects_stale_manifest(tmp_path):
    factory = tmp_path / "factory.tar.gz"
    _archive(factory)
    image = tmp_path / "image.bin"
    image.write_bytes(b"signed image")
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({"target": "esp32s3", "sha256": "0" * 64, "size": image.stat().st_size}))
    with pytest.raises(ValueError, match="SHA-256"):
        validate(factory, image, manifest)
