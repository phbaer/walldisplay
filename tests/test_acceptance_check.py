import hashlib
import json
from pathlib import Path

import pytest

from tools.acceptance_check import validate


def _factory(path: Path, image: bytes = b"signed image"):
    with path.open("wb") as stream:
        stream.seek(0x30000)
        stream.write(image)


def test_acceptance_check_validates_manifest(tmp_path):
    factory = tmp_path / "factory.bin"
    image = tmp_path / "image.bin"
    image.write_bytes(b"signed image")
    _factory(factory, image.read_bytes())
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({"target": "esp32s3", "sha256": hashlib.sha256(image.read_bytes()).hexdigest(), "size": image.stat().st_size}))
    assert "contents: complete" in "\n".join(validate(factory, image, manifest))


def test_acceptance_check_rejects_incomplete_factory_image(tmp_path):
    factory = tmp_path / "factory.bin"
    factory.write_bytes(b"incomplete")
    image = tmp_path / "image.bin"
    image.write_bytes(b"signed image")
    with pytest.raises(ValueError, match="too small"):
        validate(factory, image, None)


def test_acceptance_check_rejects_stale_manifest(tmp_path):
    factory = tmp_path / "factory.bin"
    image = tmp_path / "image.bin"
    image.write_bytes(b"signed image")
    _factory(factory, image.read_bytes())
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps({"target": "esp32s3", "sha256": "0" * 64, "size": image.stat().st_size}))
    with pytest.raises(ValueError, match="SHA-256"):
        validate(factory, image, manifest)
