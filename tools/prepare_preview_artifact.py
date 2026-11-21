#!/usr/bin/env python3
"""Fetch the exact unsigned OTA artifact produced for an approved PR label event."""
from __future__ import annotations

import argparse
import io
import json
import os
from pathlib import Path, PurePosixPath
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
import zipfile


MAX_ARCHIVE_BYTES = 10 * 1024 * 1024
MAX_TOTAL_UNCOMPRESSED_BYTES = 8 * 1024 * 1024
MAX_FILE_BYTES = 5 * 1024 * 1024
EXPECTED_FILES = {
    "walldisplay-unsigned.bin", "bootloader.bin", "partition-table.bin",
    "ota_data_initial.bin", "partitions.csv", "source-sha.txt", "preview.json",
}


def request_bytes(url: str, token: str, limit: int) -> bytes:
    request = Request(url, headers={
        "Accept": "application/json, application/zip",
        "Authorization": f"token {token}",
        "User-Agent": "walldisplay-preview-publisher",
    })
    with urlopen(request, timeout=30) as response:
        if response.status != 200:
            raise RuntimeError(f"Forgejo returned HTTP {response.status}")
        data = response.read(limit + 1)
    if len(data) > limit:
        raise RuntimeError("Forgejo response exceeds the configured size limit")
    return data


def event_matches(run: dict, pr_number: int, source_sha: str) -> bool:
    if (run.get("workflow_id") != "release-firmware.yaml"
            or run.get("event") != "pull_request"
            or run.get("commit_sha") != source_sha):
        return False
    try:
        event = run.get("event_payload")
        if isinstance(event, str):
            event = json.loads(event)
        return (
            event.get("action") == "labeled"
            and event.get("number") == pr_number
            and (event.get("label") or {}).get("name") == "firmware-preview"
        )
    except (AttributeError, TypeError, ValueError):
        return False


def find_run(server: str, repo: str, token: str, pr_number: int, source_sha: str) -> dict | None:
    data = json.loads(request_bytes(
        f"{server}/api/v1/repos/{repo}/actions/runs?limit=100", token, 4 * 1024 * 1024
    ))
    runs = data.get("workflow_runs", []) if isinstance(data, dict) else data
    if not isinstance(runs, list):
        raise RuntimeError("Forgejo returned an invalid Actions run list")
    matches = [run for run in runs if isinstance(run, dict)
               and event_matches(run, pr_number, source_sha)]
    if not matches:
        return None
    return max(matches, key=lambda run: (run.get("index_in_repo", 0), run.get("id", 0)))


def find_artifact(server: str, repo: str, token: str, run_id: int) -> dict | None:
    data = json.loads(request_bytes(
        f"{server}/api/v1/repos/{repo}/actions/artifacts?limit=100", token, 8 * 1024 * 1024
    ))
    artifacts = data.get("artifacts", []) if isinstance(data, dict) else data
    if not isinstance(artifacts, list):
        raise RuntimeError("Forgejo returned an invalid Actions artifact list")
    prefix = f"ota-preview-input-{run_id}-"
    candidates = [artifact for artifact in artifacts if isinstance(artifact, dict)
                  and artifact.get("run_id") == run_id
                  and isinstance(artifact.get("name"), str)
                  and artifact["name"].startswith(prefix) and not artifact.get("expired")]
    if not candidates:
        return None
    return max(candidates, key=lambda artifact: artifact.get("created_at", ""))


def validate_archive_url(server: str, repo: str, archive_url: str) -> None:
    if not isinstance(archive_url, str) or not archive_url:
        raise RuntimeError("Forgejo did not provide an artifact download URL")
    expected_origin = urlsplit(server)
    artifact_origin = urlsplit(archive_url)
    expected_path = f"/api/v1/repos/{repo}/actions/artifacts/"
    if (artifact_origin.scheme != "https" or artifact_origin.netloc != expected_origin.netloc
            or not artifact_origin.path.startswith(expected_path)
            or not artifact_origin.path.endswith("/zip")):
        raise RuntimeError("Artifact download URL is outside the configured Forgejo API")


def unpack_artifact(archive_bytes: bytes, output: Path, expected_sha: str,
                    server: str, repo: str, archive_url: str) -> None:
    validate_archive_url(server, repo, archive_url)
    output.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as bundle:
        infos = [info for info in bundle.infolist() if not info.is_dir()]
        names = [PurePosixPath(info.filename).name for info in infos]
        if len(names) != len(set(names)) or set(names) != EXPECTED_FILES:
            raise RuntimeError("Preview artifact has an unexpected file set")
        if sum(info.file_size for info in infos) > MAX_TOTAL_UNCOMPRESSED_BYTES:
            raise RuntimeError("Preview artifact expands beyond the permitted size")
        for info in infos:
            if info.file_size > MAX_FILE_BYTES or PurePosixPath(info.filename).is_absolute():
                raise RuntimeError("Preview artifact contains an invalid file")
            if ".." in PurePosixPath(info.filename).parts:
                raise RuntimeError("Preview artifact contains an unsafe path")
            (output / PurePosixPath(info.filename).name).write_bytes(bundle.read(info))
    if (output / "source-sha.txt").read_text(encoding="ascii").strip() != expected_sha:
        raise RuntimeError("Preview artifact does not match the labeled PR head SHA")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--pr-number", required=True, type=int)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("preview-input"))
    parser.add_argument("--timeout-seconds", type=int, default=1800)
    args = parser.parse_args()
    server = args.server.rstrip("/")
    token = os.environ.get("FORGEJO_TOKEN", "")
    if not token:
        raise SystemExit("FORGEJO_TOKEN is required to locate the PR build artifact")
    if args.pr_number <= 0 or len(args.source_sha) != 40:
        raise SystemExit("The PR number or source SHA is invalid")

    deadline = time.monotonic() + args.timeout_seconds
    while time.monotonic() < deadline:
        try:
            run = find_run(server, args.repo, token, args.pr_number, args.source_sha)
            if run is not None:
                status = run.get("status")
                if status in {"failure", "cancelled", "skipped"}:
                    raise SystemExit(f"The unsigned PR build finished with status {status}")
                if status == "success":
                    artifact = find_artifact(server, args.repo, token, run["id"])
                    if artifact is not None:
                        archive_url = artifact.get("archive_download_url", "")
                        validate_archive_url(server, args.repo, archive_url)
                        archive_bytes = request_bytes(archive_url, token, MAX_ARCHIVE_BYTES)
                        unpack_artifact(archive_bytes, args.output_dir, args.source_sha,
                                        server, args.repo, archive_url)
                        return
        except HTTPError as error:
            if error.code in {401, 403, 404}:
                raise SystemExit(f"Forgejo Actions API returned HTTP {error.code}") from error
        except (URLError, TimeoutError, json.JSONDecodeError):
            pass
        time.sleep(15)
    raise SystemExit("Timed out waiting for the matching labeled PR build artifact")


if __name__ == "__main__":
    main()
