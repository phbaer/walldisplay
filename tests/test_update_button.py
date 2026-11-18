"""Install Firmware Update button and the shared update request helper."""
import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from homeassistant.exceptions import HomeAssistantError

from custom_components.walldisplay_sync import button, runtime
from custom_components.walldisplay_sync.updates import FirmwareRelease

TOPIC = "panel/kitchen"
HASS = SimpleNamespace()
MANIFEST = "https://git.example/releases/download/v1.3.0/walldisplay-v1.3.0.json"


def _runtime(find_update=None):
    return SimpleNamespace(
        topic=TOPIC, identifier="wd_kitchen",
        update_lock=asyncio.Lock(), update_running=False, update_target="",
        find_update=find_update,
    )


class PublishUpdateTests(unittest.TestCase):
    def test_publishes_once_and_refuses_while_running(self):
        rt = _runtime()
        with patch.object(runtime.mqtt, "async_publish", new=AsyncMock()) as publish:
            self.assertTrue(asyncio.run(runtime.async_publish_update(HASS, rt, TOPIC, MANIFEST, "1.3.0")))
            publish.assert_awaited_once_with(HASS, f"{TOPIC}/cmd/update", MANIFEST, 1, False)
            self.assertTrue(rt.update_running)
            self.assertEqual(rt.update_target, "1.3.0")
            self.assertFalse(asyncio.run(runtime.async_publish_update(HASS, rt, TOPIC, MANIFEST, "1.3.0")))
            publish.assert_awaited_once()

    def test_failed_publish_clears_the_running_state(self):
        rt = _runtime()
        with patch.object(runtime.mqtt, "async_publish", new=AsyncMock(side_effect=OSError("offline"))):
            with self.assertRaises(OSError):
                asyncio.run(runtime.async_publish_update(HASS, rt, TOPIC, MANIFEST, "1.3.0"))
        self.assertFalse(rt.update_running)
        self.assertEqual(rt.update_target, "")


class InstallButtonTests(unittest.TestCase):
    def _press(self, rt):
        entity = button.WallDisplayInstallUpdateButton(rt)
        entity.hass = HASS  # Home Assistant sets this when the entity is added
        return asyncio.run(entity.async_press())

    def test_button_has_a_stable_identity_on_the_panel(self):
        entity = button.WallDisplayInstallUpdateButton(_runtime())
        self.assertEqual(entity.unique_id, "wd_kitchen_install_update")
        self.assertEqual(entity.name, "Install Firmware Update")

    def test_press_publishes_the_newest_release(self):
        release = FirmwareRelease("1.3.0", MANIFEST)
        rt = _runtime(find_update=AsyncMock(return_value=release))
        with patch.object(runtime.mqtt, "async_publish", new=AsyncMock()) as publish:
            self._press(rt)
        publish.assert_awaited_once_with(HASS, f"{TOPIC}/cmd/update", MANIFEST, 1, False)

    def test_press_reports_when_the_panel_is_current(self):
        rt = _runtime(find_update=AsyncMock(return_value=None))
        with patch.object(runtime.mqtt, "async_publish", new=AsyncMock()) as publish:
            with self.assertRaisesRegex(HomeAssistantError, "newest signed firmware"):
                self._press(rt)
        publish.assert_not_awaited()

    def test_press_reports_an_update_already_running(self):
        rt = _runtime(find_update=AsyncMock(return_value=FirmwareRelease("1.3.0", MANIFEST)))
        rt.update_running = True
        with patch.object(runtime.mqtt, "async_publish", new=AsyncMock()) as publish:
            with self.assertRaisesRegex(HomeAssistantError, "already in progress"):
                self._press(rt)
        publish.assert_not_awaited()

    def test_press_before_setup_reports_it(self):
        with self.assertRaisesRegex(HomeAssistantError, "still starting"):
            self._press(_runtime(find_update=None))


if __name__ == "__main__":
    unittest.main()
