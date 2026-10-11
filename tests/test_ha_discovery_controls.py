"""Validate real firmware discovery against HA schemas and endpoint coverage."""
from pathlib import Path
import json
import asyncio
from types import SimpleNamespace
from homeassistant.core import HomeAssistant
import subprocess

from homeassistant.components.mqtt import abbreviations
from homeassistant.components.mqtt import button, number, select, sensor, switch, text
from custom_components.walldisplay_sync.runtime import WallDisplayRuntime

ROOT = Path(__file__).resolve().parents[1]


def test_firmware_device_endpoints(tmp_path):
    (tmp_path / 'mqtt_client.h').write_text('''
#pragma once
typedef void *esp_mqtt_client_handle_t;
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char *, const char *, int, int, int);
''')
    (tmp_path / 'compat.h').write_text('''
#include <stddef.h>
size_t strlcpy(char *, const char *, size_t);
#define esp_err_to_name(e) "error"
''')
    (tmp_path / 'discovery.c').write_text(r'''
#include <stdio.h>
#include <string.h>
#include "walldisplay/app_config.h"
#include "walldisplay/ha_discovery.h"
static app_config_t config;
size_t strlcpy(char *dst, const char *src, size_t n) {
    size_t length = strlen(src);
    if (n) { size_t copy = length < n - 1 ? length : n - 1; memcpy(dst, src, copy); dst[copy] = 0; }
    return length;
}
const app_config_t *app_config_get(void) { return &config; }
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic,
        const char *payload, int length, int qos, int retain) {
    (void)client; (void)length; (void)qos;
    if (!retain) return -1;
    printf("%s\t%s\n", topic, payload);
    return 1;
}
int main(void) {
    strcpy(config.base_topic, "wd/test-panel");
    strcpy(config.discovery_prefix, "homeassistant");
    strcpy(config.display_name, "Test Panel");
    panel_layout_defaults(&config.layout);
    return ha_discovery_publish_all((void *)1);
}
''')
    executable = tmp_path / 'discovery'
    subprocess.run([
        'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-include', str(tmp_path / 'compat.h'),
        '-I', str(tmp_path), '-I', str(ROOT / 'tests/stubs'), '-I', str(ROOT / 'include'),
        '-I', str(ROOT / 'managed_components/espressif__cjson/cJSON'),
        str(tmp_path / 'discovery.c'), str(ROOT / 'src/ha_discovery.c'),
        str(ROOT / 'src/page_manager.c'), str(ROOT / 'managed_components/espressif__cjson/cJSON/cJSON.c'),
        '-lm', '-o', str(executable),
    ], check=True)
    output = subprocess.check_output([str(executable)], text=True)
    schemas = {m.__name__.rsplit('.', 1)[-1]: m.DISCOVERY_SCHEMA for m in (button, number, select, sensor, switch, text)}
    commands, states, records = set(), set(), {}
    configs = []
    identifiers = set()
    for line in output.splitlines():
        topic, payload = line.split('\t', 1)
        component = topic.split('/')[1]
        raw = json.loads(payload)
        expanded = {abbreviations.ABBREVIATIONS.get(k, k): v for k, v in raw.items()}
        expanded['device'] = {abbreviations.DEVICE_ABBREVIATIONS.get(k, k): v for k, v in raw['dev'].items()}
        configs.append((component, expanded))
        records[raw['uniq_id']] = raw
        identifiers.add(tuple(raw['dev']['ids']))
        if 'cmd_t' in raw:
            commands.add(raw['cmd_t'].removeprefix('wd/test-panel/'))
            assert not raw.get('ret', False), topic
        if 'stat_t' in raw: states.add(raw['stat_t'].removeprefix('wd/test-panel/'))
    async def validate_all():
        hass = HomeAssistant(str(tmp_path))
        for component, expanded in configs:
            schemas[component](expanded)
        hass.states.async_set('number.panel_dim', '20', {'dim_after': 0, 'off_after': 0, 'dim_percent': 20})
        from homeassistant.helpers.template import Template
        template = Template(records['walldisplay_wd_test_panel_dim_percent']['cmd_tpl'], hass)
        rendered = template.async_render({'value': 30, 'this': SimpleNamespace(entity_id='number.panel_dim')}, parse_result=False)
        assert json.loads(rendered) == {'dim_after': 0, 'off_after': 0, 'dim_percent': 30}
        volume = Template(records['walldisplay_wd_test_panel_media_volume']['val_tpl'], hass)
        assert float(volume.async_render({'value_json': {'volume_level': 0.5}}, parse_result=False)) == 50
    asyncio.run(validate_all())
    expected = {
        'set/name', 'set/clock', 'set/date', 'set/blueprint_info', 'set/weather', 'set/media',
        'set/media/power', 'set/pages', 'set/display_power', 'set/screenshots_enabled',
        'cmd/update', 'cmd/config/base_topic', 'cmd/config/default_page', 'cmd/config/screenshot_token',
        'cmd/config/ap', 'cmd/config/factory_reset', 'cmd/config/reboot', 'cmd/wake', 'cmd/sync',
        'cmd/page', 'cmd/screenshot', 'cmd/media/volume',
    }
    expected.update(f'cmd/media/{a}' for a in ('previous', 'play_pause', 'next', 'power_off', 'volume_down', 'volume_up'))
    for i in range(1, 6):
        expected.update((f'cmd/button{i}', f'set/button{i}/label', f'set/button{i}/state', f'cmd/media/favorite{i}',
                         f'set/media/favorite{i}/label', f'set/media/favorite{i}/icon'))
    for i in range(1, 7): expected.update((f'cmd/grid{i}', f'set/grid/{i}'))
    for i in range(1, 5): expected.update((f'set/chip{i}', f'set/chip{i}/color'))
    assert expected <= commands
    assert {'state/device', 'state/update', 'state/screenshot', 'state/config/error', 'state/config/pages',
            'state/config/default_page', 'state/config/base_topic', 'state/page', 'state/contract_version',
            'state/screenshots_enabled', 'state/display_power', 'state/media/power', 'state/hostname'} <= states
    assert identifiers == {('walldisplay_wd_test_panel_device',)}
    assert records['walldisplay_wd_test_panel_screenshot']['pl_prs'] == 'screenshot'
    token = records['walldisplay_wd_test_panel_screenshot_token']
    assert token['mode'] == 'password' and 'stat_t' not in token
    for suffix in ('weather', 'media', 'device_diagnostics', 'screenshot_status', 'update_status', 'page_layout'):
        assert 'json_attr_t' in records[f'walldisplay_wd_test_panel_{suffix}']


def test_native_entities_share_firmware_device_identity():
    runtime = WallDisplayRuntime(None, 'wd/Test-Panel.ä', 'Test Panel', 'legacy-entry')
    assert runtime.device_identifiers == {('mqtt', 'walldisplay_wd_Test_Panel____device')}
