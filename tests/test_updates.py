"""Firmware release discovery and version comparison tests."""
import json
import unittest
from types import SimpleNamespace
from unittest.mock import patch

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

    def test_newer_release_comparison_handles_dotted_prereleases(self):
        release = FirmwareRelease("1.0.0-rc.16", "https://git.example/manifest.json")
        self.assertTrue(is_newer("v1.0.0-rc.14.1", release))

    def test_api_url_must_use_https(self):
        import asyncio
        self.assertIsNone(asyncio.run(async_latest_release(object(), "http://git.example/releases")))

    def test_release_with_unknown_version_is_ignored(self):
        self.assertIsNone(_release_candidate({
            "tag_name": "nightly",
            "assets": [{"name": "walldisplay-nightly.json", "browser_download_url": "https://git.example/nightly.json"}],
        }))


class FirmwareUpdateRepairTests(unittest.IsolatedAsyncioTestCase):
    async def _run_repair(self, *, runtime=None, publish_error=None):
        from custom_components.walldisplay_sync.repairs import FirmwareUpdateRepairFlow
        import asyncio

        flow = FirmwareUpdateRepairFlow({
            "entry_id": "entry",
            "topic": "panel/test",
            "manifest_url": "https://git.example/manifest.json",
            "panel_name": "Test panel",
            "current_version": "0.5.0",
            "latest_version": "1.0.0",
        })
        runtime = runtime or SimpleNamespace(update_lock=asyncio.Lock(), update_running=False, update_target="")
        entry = SimpleNamespace(entry_id="entry", runtime_data=runtime)
        flow.hass = SimpleNamespace(config_entries=SimpleNamespace(async_entries=lambda _domain: [entry]))
        published = []

        async def publish(_hass, _topic, _payload, _qos, _retain):
            if publish_error:
                raise publish_error
            published.append((_topic, _payload, _retain))

        with patch("custom_components.walldisplay_sync.repairs.mqtt.async_publish", side_effect=publish):
            result = await flow.async_step_confirm({})
        return result, runtime, published

    async def test_repair_reports_accepted_request_and_keeps_issue(self):
        result, runtime, published = await self._run_repair()
        self.assertEqual(result["reason"], "update_started")
        self.assertEqual(result["description_placeholders"]["latest_version"], "1.0.0")
        self.assertTrue(runtime.update_running)
        self.assertEqual(published[0][1], "https://git.example/manifest.json")

    async def test_repair_rejects_second_request_while_running(self):
        import asyncio
        runtime = SimpleNamespace(update_lock=asyncio.Lock(), update_running=True, update_target="1.0.0")
        result, _, published = await self._run_repair(runtime=runtime)
        self.assertEqual(result["reason"], "update_in_progress")
        self.assertEqual(published, [])

    async def test_repair_reports_publish_error(self):
        result, runtime, _ = await self._run_repair(publish_error=RuntimeError("MQTT unavailable"))
        self.assertEqual(result["reason"], "update_failed")
        self.assertFalse(runtime.update_running)


if __name__ == "__main__":
    unittest.main()
