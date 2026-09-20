"""HA schema, YAML round-trip and blueprint/integration page contract checks."""
import json
from pathlib import Path
import unittest
import yaml
from homeassistant.helpers import selector
import voluptuous as vol
from jinja2 import Environment
from custom_components.walldisplay_sync.config_flow import (
    _complete_config, _grid_schema, _merge_form, WallDisplayConfigFlow,
)
from custom_components.walldisplay_sync.pages import MAX_PAGE_SLOTS, layout_payload, grid_payload

ROOT = Path(__file__).resolve().parents[1]

class InputLoader(yaml.SafeLoader):
    pass
InputLoader.add_constructor("!input", lambda loader, node: ("input", loader.construct_scalar(node)))


def mappings(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from mappings(child)
    elif isinstance(value, list):
        for child in value:
            yield from mappings(child)


class PageConfigurationTests(unittest.TestCase):
    def test_complete_yaml_roundtrip(self):
        config = _complete_config({"panel_topic": "panel/test", "page1": "buttons", "page2": "none",
                                   "default_page": "buttons", "grid1_label": "Lights", "grid1_state_entity": "light.room"})
        config["grid6_label"] = "Movie night"
        config["weather_title"] = "Météo"
        restored = _complete_config(yaml.safe_load(yaml.safe_dump(config, allow_unicode=True)))
        self.assertEqual(config, restored)
        self.assertEqual(layout_payload(restored)["pages"], ["buttons"])
        self.assertEqual(config["media_entity"], "")
        self.assertEqual(config["about_title"], "About")

    def test_complete_examples(self):
        integration = _complete_config(yaml.safe_load((ROOT / "config/examples/panel_integration.yaml").read_text()))
        automation = yaml.safe_load((ROOT / "config/examples/panel_blueprint.yaml").read_text())
        blueprint = automation["use_blueprint"]["input"]
        self.assertEqual(layout_payload(integration), layout_payload(blueprint))
        self.assertEqual(integration["grid1_state_entity"], blueprint["grid1_state_entity"])

    def test_layout_supports_five_ordered_slots(self):
        config = _complete_config({
            "panel_topic": "panel/test",
            "page1": "weather",
            "page2": "media",
            "page3": "buttons",
            "page4": "about",
            "page5": "none",
            "default_page": "about",
        })
        self.assertEqual(MAX_PAGE_SLOTS, 5)
        self.assertEqual(layout_payload(config)["pages"], ["weather", "media", "buttons", "about"])
        self.assertEqual(config["page5"], "none")

    def test_build_time_yaml_defaults(self):
        import ast
        import subprocess
        import sys
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "panel.yaml"
            output = Path(directory) / "defaults.h"
            text = (ROOT / "config/panel_config.example.yaml").read_text()
            source.write_text(text.replace("page1: weather", "page1: buttons").replace("default_page: weather", "default_page: buttons"))
            command = [sys.executable, str(ROOT / "tools/generate_app_config_defaults.py"), str(source), str(output)]
            subprocess.run(command, check=True, capture_output=True)
            macro = next(line.split(" ", 2)[2] for line in output.read_text().splitlines() if line.startswith("#define APPCFG_DEFAULT_LAYOUT_JSON "))
            generated_layout = json.loads(ast.literal_eval(macro))
            self.assertEqual(generated_layout["pages"], ["buttons", "media"])
            self.assertEqual(generated_layout["titles"]["about"], "About")
            source.write_text(text.replace("page2: media", "page2: weather"))
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)

    def test_configuration_migration(self):
        from custom_components.walldisplay_sync.configuration import migrate_configuration
        data = migrate_configuration({"panel_topic": "panel/test", "grid1_label": "Keep me"})
        self.assertEqual(data["schema_version"], 2)
        self.assertEqual(data["grid1_label"], "Keep me")
        self.assertFalse(data["screenshots_enabled"])
        self.assertEqual(migrate_configuration(data), data)
        with self.assertRaises(vol.Invalid):
            migrate_configuration({"panel_topic": "panel/test", "schema_version": 99})

    def test_reject_invalid_configuration(self):
        for change in ({"page2": "weather"}, {"page1": "none", "page2": "none"},
                       {"page1": "buttons", "default_page": "weather"}, {"page1": "camera"},
                       {"weather_title": "é" * 25}, {"grid1_label": "é" * 49},
                       {"grid1_state_entity": "sensor.temperature"}, {"surprise": True},
                       {"panel_topic": "panel/#"}, {"update_api_url": "http://git.example/releases"},
                       {"panel_name": "\x01bad"}, {"panel_name": "!" * 65}):
            with self.subTest(change=change), self.assertRaises((ValueError, vol.Invalid)):
                _complete_config({"panel_topic": "panel/test", **change})

    def test_display_name_is_preserved_and_hostname_generated(self):
        config = _complete_config({"panel_topic": "panel/test", "panel_name": "Living Room"})
        self.assertEqual(config["panel_name"], "Living Room")
        self.assertEqual(config["panel_hostname"], "living-room")

    def test_clear_entity_in_form(self):
        data = {"grid1_label": "Lights", "grid1_state_entity": "light.room"}
        _merge_form(data, {"grid1_label": "Scene"}, _grid_schema(data))
        self.assertEqual(data["grid1_state_entity"], "")

    def test_blueprint_contract_matches_integration(self):
        blueprint = yaml.load((ROOT / "config/blueprints/automation/walldisplay/mqtt_sync.yaml").read_text(), Loader=InputLoader)
        from homeassistant.components.blueprint.schemas import BLUEPRINT_SCHEMA
        BLUEPRINT_SCHEMA(blueprint)
        inputs = blueprint["blueprint"]["input"]
        for name, spec in inputs.items():
            if "selector" in spec:
                selector.validate_selector(spec["selector"])
        defaults = {key: spec.get("default", "") for key, spec in inputs.items()}
        defaults.update(page1="buttons", page2="weather", page3="about", default_page="buttons", grid1_label="Living room")
        environment = Environment()
        publishes = [value["data"] for value in mappings(blueprint["actions"]) if value.get("action") == "mqtt.publish"]
        layout = next(item for item in publishes if item["topic"].endswith("/set/pages"))
        actual = json.loads(environment.from_string(layout["payload"]).render(defaults))
        self.assertEqual(actual, layout_payload(defaults))
        for slot in range(1, 7):
            item = next(item for item in publishes if item["topic"].endswith(f"/set/grid/{slot}"))
            actual = json.loads(environment.from_string(item["payload"]).render(defaults))
            self.assertEqual(actual, grid_payload(defaults, slot, "stateless"))
        declared = set(inputs)
        for value in mappings(blueprint):
            for child in value.values():
                if isinstance(child, tuple) and child[0] == "input":
                    self.assertIn(child[1], declared)


class PageFlowTests(unittest.IsolatedAsyncioTestCase):
    async def test_updates_form_schema_is_serializable(self):
        from voluptuous_serialize import convert

        flow = WallDisplayConfigFlow()
        flow._data = {"panel_topic": "panel/test"}
        result = await flow.async_step_updates()
        self.assertEqual(result["type"], "form")
        self.assertEqual(convert(result["data_schema"])[0]["type"], "string")

    async def test_expanded_blueprint_automation_schema(self):
        from homeassistant.core import HomeAssistant
        from homeassistant.components.automation.config import PLATFORM_SCHEMA
        HomeAssistant("/tmp/walldisplay-ha-schema-test")
        source = yaml.load((ROOT / "config/blueprints/automation/walldisplay/mqtt_sync.yaml").read_text(), Loader=InputLoader)
        values = yaml.safe_load((ROOT / "config/examples/panel_blueprint.yaml").read_text())["use_blueprint"]["input"]
        def resolve(value):
            if isinstance(value, tuple):
                return values[value[1]]
            if isinstance(value, list):
                return [resolve(child) for child in value]
            if isinstance(value, dict):
                return {key: resolve(child) for key, child in value.items() if key != "blueprint"}
            return value
        PLATFORM_SCHEMA(resolve(source))

    async def test_invalid_layout_keeps_form_data(self):
        flow = WallDisplayConfigFlow()
        flow._data = {"panel_topic": "panel/test", "page1": "weather"}
        result = await flow.async_step_pages({"page1": "weather", "page2": "weather"})
        self.assertEqual(result["errors"], {"base": "invalid_pages"})
        self.assertNotIn("page2", flow._data)

    async def test_form_edits_are_visible_in_yaml_editor(self):
        flow = WallDisplayConfigFlow()
        flow._data = {"panel_topic": "panel/test"}
        await flow.async_step_grid({"grid1_label": "Movie night"})
        result = await flow.async_step_yaml()
        marker = next(iter(result["data_schema"].schema))
        exported = marker.default()
        self.assertEqual(exported["grid1_label"], "Movie night")
        self.assertIn("dim_after", exported)

class GridRuntimeTests(unittest.IsolatedAsyncioTestCase):
    async def test_grid_toggle_event_and_unavailable_entity(self):
        from contextlib import ExitStack
        from types import SimpleNamespace
        from unittest.mock import AsyncMock, Mock, patch
        from custom_components import walldisplay_sync as integration

        states = {"light.room": SimpleNamespace(state="off", attributes={})}
        hass = SimpleNamespace(states=SimpleNamespace(get=states.get),
                               services=SimpleNamespace(async_call=AsyncMock()),
                               config=SimpleNamespace(internal_url=None, external_url=None),
                               config_entries=SimpleNamespace(async_forward_entry_setups=AsyncMock()))
        config = _complete_config({"panel_topic": "panel/test", "page1": "buttons", "page2": "none",
                                   "default_page": "buttons", "grid1_label": "Lights",
                                   "grid1_state_entity": "light.room", "grid2_label": "Scene", "footer1_label": "Footer action"})
        entry = SimpleNamespace(data=config, options={}, title="Room", unique_id="panel/test", entry_id="test",
                                async_on_unload=Mock(), add_update_listener=Mock())
        subscriptions = {}
        async def subscribe(hass, topic, callback, qos):
            subscriptions[topic] = callback
            return Mock()
        with ExitStack() as stack:
            publish = stack.enter_context(patch.object(integration.mqtt, "async_publish", new_callable=AsyncMock))
            stack.enter_context(patch.object(integration.mqtt, "async_subscribe", side_effect=subscribe))
            for name in ("async_register_cache", "async_unregister_cache", "ArtworkCache",
                         "async_track_state_change_event", "async_track_time_interval"):
                stack.enter_context(patch.object(integration, name))
            self.assertTrue(await integration.async_setup_entry(hass, entry))
            payloads = {call.args[1]: call.args[2] for call in publish.call_args_list}
            self.assertEqual(json.loads(payloads["panel/test/set/grid/1"])["state"], "off")
            callback = subscriptions["panel/test/cmd/grid1"]
            await callback(SimpleNamespace(topic="panel/test/cmd/grid1", payload="press"))
            hass.services.async_call.assert_awaited_once_with("homeassistant", "toggle", target={"entity_id": "light.room"}, blocking=False)
            event = Mock()
            entry.runtime_data.events[7] = event  # footer slots 1–5, then grid slot 2
            await subscriptions["panel/test/cmd/grid2"](SimpleNamespace(topic="panel/test/cmd/grid2", payload="press"))
            event.press.assert_called_once()
            footer = Mock()
            entry.runtime_data.events[1] = footer
            footer_callback = subscriptions["panel/test/cmd/button1"]
            await footer_callback(SimpleNamespace(topic="panel/test/cmd/button1", payload="toggle", retain=True))
            footer.press.assert_not_called()
            await footer_callback(SimpleNamespace(topic="panel/test/cmd/button1", payload="toggle", retain=False))
            footer.press.assert_called_once()
            await callback(SimpleNamespace(topic="panel/test/cmd/grid1", payload="press", retain=True))
            self.assertEqual(hass.services.async_call.await_count, 1)
            states["light.room"].state = "unavailable"
            await callback(SimpleNamespace(topic="panel/test/cmd/grid1", payload="press"))
            self.assertEqual(hass.services.async_call.await_count, 1)
            # Hidden controls and malformed payloads must not invoke actions.
            await subscriptions["panel/test/cmd/grid3"](SimpleNamespace(topic="panel/test/cmd/grid3", payload="press"))
            await callback(SimpleNamespace(topic="panel/test/cmd/grid1", payload="bad"))
            self.assertEqual(hass.services.async_call.await_count, 1)


if __name__ == "__main__":
    unittest.main()
