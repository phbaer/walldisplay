"""Firmware release discovery and version comparison tests."""
import unittest

from custom_components.walldisplay_sync.updates import (
    FirmwareRelease,
    _release_candidate,
    async_latest_release,
    is_newer,
)


class FirmwareUpdateTests(unittest.TestCase):
    def test_repair_translation_uses_one_description_form(self):
        import json
        from pathlib import Path

        root = Path(__file__).parents[1] / "custom_components/walldisplay_sync"
        issue = json.loads((root / "strings.json").read_text())["issues"]["firmware_update_available"]
        translated_issue = json.loads((root / "translations/en.json").read_text())["issues"]["firmware_update_available"]
        self.assertIn("{current_version}", issue["title"])
        self.assertIn("{latest_version}", issue["title"])
        self.assertNotIn("description", issue)
        self.assertIn("description", issue["fix_flow"]["step"]["confirm"])
        self.assertEqual(translated_issue, issue)

    def test_release_candidate_requires_signed_manifest_asset(self):
        release = {
            "tag_name": "v1.2.0",
            "draft": False,
            "assets": [{
                "name": "walldisplay-v1.2.0.json",
                "browser_download_url": "https://git.example/releases/download/v1.2.0/walldisplay-v1.2.0.json",
            }],
        }
        self.assertEqual(
            _release_candidate(release),
            FirmwareRelease("1.2.0", "https://git.example/releases/download/v1.2.0/walldisplay-v1.2.0.json"),
        )
        self.assertIsNone(_release_candidate({**release, "assets": []}))
        self.assertIsNone(_release_candidate({**release, "draft": True}))

    def test_newer_release_comparison_handles_prereleases(self):
        release = FirmwareRelease("1.0.0-rc.2", "https://git.example/manifest.json")
        self.assertTrue(is_newer("1.0.0-rc.1", release))
        self.assertFalse(is_newer("1.0.0", release))
        self.assertFalse(is_newer("unknown", release))

    def test_api_url_must_use_https(self):
        import asyncio
        self.assertIsNone(asyncio.run(async_latest_release(object(), "http://git.example/releases")))

    def test_release_with_unknown_version_is_ignored(self):
        self.assertIsNone(_release_candidate({
            "tag_name": "nightly",
            "assets": [{"name": "walldisplay-nightly.json", "browser_download_url": "https://git.example/nightly.json"}],
        }))


if __name__ == "__main__":
    unittest.main()
