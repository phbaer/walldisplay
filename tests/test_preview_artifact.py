import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location(
    "prepare_preview_artifact", Path(__file__).resolve().parents[1] / "tools/prepare_preview_artifact.py")
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


class PreviewArtifactTests(unittest.TestCase):
    def test_matches_only_the_labeled_pr_build_for_the_exact_head(self):
        event = {
            "action": "label_updated",
            "number": 30,
            "label": {"name": "firmware-preview"},
            "pull_request": {"labels": [{"name": "firmware-preview"}]},
        }
        run = {
            "workflow_id": "release-firmware.yaml",
            "event": "pull_request",
            "commit_sha": "a" * 40,
            "event_payload": json.dumps(event),
        }
        self.assertTrue(preview.event_matches(run, 30, "a" * 40))
        self.assertFalse(preview.event_matches(run, 30, "b" * 40))
        self.assertFalse(preview.event_matches(run, 31, "a" * 40))
        self.assertFalse(preview.event_matches({**run, "workflow_id": "other.yaml"}, 30, "a" * 40))
        run["event_payload"] = json.dumps({**event, "action": "labeled"})
        self.assertTrue(preview.event_matches(run, 30, "a" * 40))
        run["event_payload"] = json.dumps({**event, "pull_request": {"labels": []}})
        self.assertFalse(preview.event_matches(run, 30, "a" * 40))

    def test_unpacks_only_expected_files_and_checks_source_sha(self):
        contents = {name: b"preview" for name in preview.EXPECTED_FILES}
        contents["source-sha.txt"] = b"a" * 40 + b"\n"
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as bundle:
            for name, value in contents.items():
                bundle.writestr(name, value)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "preview"
            preview.unpack_artifact(
                archive.getvalue(), output, "a" * 40, "https://git.baer.one",
                "phbaer/walldisplay",
                "https://git.baer.one/api/v1/repos/phbaer/walldisplay/actions/artifacts/7/zip",
            )
            self.assertEqual({path.name for path in output.iterdir()}, preview.EXPECTED_FILES)
            self.assertEqual((output / "source-sha.txt").read_text().strip(), "a" * 40)

    def test_rejects_external_downloads_and_wrong_artifact_contents(self):
        with self.assertRaises(RuntimeError):
            preview.validate_archive_url(
                "https://git.baer.one", "phbaer/walldisplay", "https://attacker.invalid/artifact.zip")
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("unexpected.txt", b"x")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(RuntimeError):
                preview.unpack_artifact(
                    archive.getvalue(), Path(temporary) / "preview", "a" * 40,
                    "https://git.baer.one", "phbaer/walldisplay",
                    "https://git.baer.one/api/v1/repos/phbaer/walldisplay/actions/artifacts/7/zip",
                )

    def test_selects_newest_artifact_for_the_exact_workflow_run(self):
        payload = [
            {"run_id": 42, "name": "ota-preview-input-42-1", "expired": False,
             "created_at": "2026-11-21T10:00:00Z"},
            {"run_id": 42, "name": "ota-preview-input-42-2", "expired": False,
             "created_at": "2026-11-21T10:01:00Z"},
            {"run_id": 41, "name": "ota-preview-input-41-1", "expired": False,
             "created_at": "2026-11-21T10:02:00Z"},
        ]
        with patch.object(preview, "request_bytes", return_value=json.dumps(payload).encode()):
            artifact = preview.find_artifact("https://git.baer.one", "phbaer/walldisplay", "token", 42)
        self.assertEqual(artifact["name"], "ota-preview-input-42-2")


if __name__ == "__main__":
    unittest.main()
