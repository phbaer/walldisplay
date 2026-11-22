#!/usr/bin/env python3
"""Sign a reviewed preview image and publish it as a Forgejo prerelease."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

from firmware_release import package, validate_tag


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--pr-number", required=True, type=int)
    parser.add_argument("--server", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--token", required=True)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
        raise SystemExit("Source SHA must be a full lowercase commit hash")
    if args.pr_number <= 0:
        raise SystemExit("PR number must be positive")

    key_value = os.environ.pop("OTA_SIGNING_KEY_PEM", "")
    if not key_value:
        raise SystemExit("OTA_SIGNING_KEY_PEM is required to sign the preview")
    input_dir = args.input_dir.resolve(strict=True)
    expected_files = {
        "walldisplay-unsigned.bin", "bootloader.bin", "partition-table.bin",
        "ota_data_initial.bin", "partitions.csv", "source-sha.txt", "preview.json",
    }
    if {path.name for path in input_dir.iterdir()} != expected_files:
        raise SystemExit("Preview input artifact has an unexpected file set")
    if (input_dir / "source-sha.txt").read_text().strip() != args.source_sha:
        raise SystemExit("Preview input artifact does not match the selected PR commit")
    try:
        metadata = json.loads((input_dir / "preview.json").read_text())
        tag = metadata["tag"]
        version = metadata["version"]
        run_id = metadata["run_id"]
        run_attempt = metadata["run_attempt"]
        metadata_pr = metadata["pr_number"]
        metadata_sha = metadata["source_sha"]
    except (KeyError, TypeError, ValueError) as error:
        raise SystemExit("Preview metadata is invalid") from error
    if not isinstance(tag, str) or not isinstance(version, str):
        raise SystemExit("Preview metadata version fields are invalid")
    validate_tag(tag)
    if (tag.removeprefix("v") != version
            or metadata_sha != args.source_sha or metadata_pr != args.pr_number
            or not isinstance(run_id, int) or run_id <= 0
            or not isinstance(run_attempt, int) or run_attempt <= 0):
        raise SystemExit("Preview metadata does not match the selected PR commit")
    base, separator, suffix = version.partition("-pr.")
    if (not separator or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", suffix)
            or suffix.split(".", 1)[0] != str(args.pr_number)
            or tag != f"v{base}-pr.{args.pr_number}.{run_id}.{run_attempt}"):
        raise SystemExit("Preview tag does not match the PR and build run")

    key_bytes = key_value.encode()
    try:
        key = serialization.load_pem_private_key(key_bytes, password=None)
    except (ValueError, TypeError) as error:
        raise SystemExit("OTA_SIGNING_KEY_PEM is not a valid unencrypted private PEM") from error
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 3072:
        raise SystemExit("OTA_SIGNING_KEY_PEM must be an RSA-3072 private key")

    with tempfile.TemporaryDirectory(prefix="walldisplay-preview-sign-") as temporary:
        temporary_dir = Path(temporary)
        key_file = temporary_dir / "ota-signing-key.pem"
        key_file.write_bytes(key_bytes)
        key_file.chmod(0o600)
        signed = temporary_dir / "walldisplay-signed.bin"
        subprocess.run([
            sys.executable, "-m", "espsecure", "sign_data", "--version", "2",
            "--keyfile", str(key_file), "--output", str(signed),
            str(input_dir / "walldisplay-unsigned.bin"),
        ], check=True)
        subprocess.run([
            sys.executable, "tools/check_signed_firmware.py",
            "--image", str(signed), "--key", str(key_file),
        ], check=True)

        build = Path("build-preview-input")
        build.mkdir(exist_ok=False)
        (build / "bootloader").mkdir()
        (build / "partition_table").mkdir()
        shutil.copyfile(signed, build / "walldisplay.bin")
        shutil.copyfile(input_dir / "bootloader.bin", build / "bootloader/bootloader.bin")
        shutil.copyfile(input_dir / "partition-table.bin", build / "partition_table/partition-table.bin")
        shutil.copyfile(input_dir / "ota_data_initial.bin", build / "ota_data_initial.bin")
        shutil.copyfile(input_dir / "partitions.csv", Path("partitions.csv"))
        package(tag, args.server, args.repo, build=build, source_sha=args.source_sha)

    request = Request(
        f"{args.server.rstrip('/')}/api/v1/repos/{args.repo}/pulls/{args.pr_number}",
        headers={"Authorization": f"Bearer {args.token}"},
    )
    with urlopen(request, timeout=30) as response:
        pr = json.load(response)
    if pr["state"] != "open" or pr["head"]["sha"] != args.source_sha:
        print("Skipping publication because the reviewed PR head is no longer open and current")
        return

    notes = Path("preview-release-notes.md")
    notes.write_text(
        f"Signed Home Assistant OTA preview for PR #{args.pr_number}.\n\n"
        f"Built from commit `{args.source_sha}`. This prerelease is intended for testing and "
        "uses the project's trusted OTA signing key. A later preview or stable release may supersede it.\n"
    )
    publisher = [
        sys.executable, "tools/publish_forgejo_release.py",
        "--server", args.server, "--repo", args.repo, "--token", args.token,
        "--tag", tag, "--title", f"Wall Display PR #{args.pr_number} preview {tag}",
        "--notes-file", str(notes), "--release-dir", "dist",
        "--target-commitish", args.source_sha, "--prerelease",
    ]
    subprocess.run(publisher, check=True)


if __name__ == "__main__":
    main()
