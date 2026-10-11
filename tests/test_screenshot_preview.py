"""Screenshot proxy conversion, authentication, error and stale-result handling."""
import asyncio
from io import BytesIO
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from homeassistant.core import HomeAssistant

import pytest
import voluptuous as vol
from PIL import Image
from custom_components.walldisplay_sync.image import _png, _host, WallDisplayScreenshot
from custom_components.walldisplay_sync.runtime import WallDisplayRuntime
from custom_components.walldisplay_sync.configuration import _complete_config


def bmp():
    output = BytesIO()
    Image.new('RGB', (480, 480), 'red').save(output, format='BMP')
    return output.getvalue()


def test_preview_validation():
    assert _png(bmp()).startswith(b'\x89PNG')
    with pytest.raises(ValueError): _png(b'not a BMP')
    assert _host('10.0.2.73') == '10.0.2.73'
    assert _host('127.0.0.1') is None
    assert _host('example.com') is None
    assert _host('8.8.8.8') is None
    assert _host('fd12:3456::73') == '[fd12:3456::73]'
    for value in ('::', '::1', 'fe80::73', 'fe80::73%eth0', 'ff02::1',
                  '2001:4860:4860::8888', '::ffff:10.0.2.73', '2001:db8::73', 'fd12::73%eth0'):
        assert _host(value) is None
    with pytest.raises(vol.Invalid): _complete_config({'panel_topic': 'panel/test', 'screenshot_token': 'short'})


def test_authenticated_preview_and_error(tmp_path):
    async def run():
        hass = HomeAssistant(str(tmp_path))
        token = 'a' * 32
        entity = WallDisplayScreenshot(hass, WallDisplayRuntime(hass, 'panel/test', 'Panel', 'id'), token)
        entity.hass = hass
        entity.async_write_ha_state = Mock()
        async def chunks(_size): yield bmp()
        response = SimpleNamespace(status=200, content=SimpleNamespace(iter_chunked=chunks))
        context = AsyncMock()
        context.__aenter__.return_value = response
        session = SimpleNamespace(get=Mock(return_value=context))
        async def send(suffix, payload):
            await entity._message(SimpleNamespace(topic='panel/test/'+suffix, payload=payload))
        with patch('custom_components.walldisplay_sync.image.async_get_clientsession', return_value=session):
            await send('status', 'online')
            await send('state/device', '{"ip":"10.0.2.73"}')
            await send('state/screenshot', '{"state":"ready","name":"screenshot","url":"http://evil.example/"}')
            assert (await entity.async_image()).startswith(b'\x89PNG')
            assert session.get.call_args.args[0] == 'http://10.0.2.73:8080/screenshot.bmp?name=screenshot'
            assert session.get.call_args.kwargs['headers']['Authorization'] == 'Bearer '+token
            assert session.get.call_args.kwargs['allow_redirects'] is False
            assert token not in str(entity.extra_state_attributes)
            await send('state/device', '{"ip":"10.0.2.73"}')
            assert session.get.call_count == 1
            response.status = 401
            await send('state/screenshot', '{"state":"ready","name":"other"}')
            assert entity.extra_state_attributes['preview_error'] == 'http_401'
            await send('state/screenshot', '{"state":"error","name":"other","error":"ESP_FAIL","stage":"storage"}')
            assert entity.extra_state_attributes['capture_error'] == 'ESP_FAIL'
            response.status = 200
            original = await entity.async_image()
            async def stale_chunks(_size):
                entity._generation += 1
                yield bmp()
            response.content.iter_chunked = stale_chunks
            await send('state/screenshot', '{"state":"ready","name":"stale"}')
            assert await entity.async_image() is original
            await send('status', 'offline')
            assert await entity.async_image() is None
            assert not entity.available
    asyncio.run(run())


def test_preview_only_does_not_publish_sync(tmp_path):
    from custom_components import walldisplay_sync as integration
    async def run():
        hass = SimpleNamespace(config_entries=SimpleNamespace(async_forward_entry_setups=AsyncMock(), async_unload_platforms=AsyncMock(return_value=True)))
        entry = SimpleNamespace(data={'panel_topic':'panel/test','screenshot_preview_only':True}, options={}, title='Panel', unique_id='panel/test', entry_id='id', async_on_unload=Mock(), add_update_listener=Mock())
        with patch.object(integration.ir, 'async_delete_issue'), patch.object(integration, 'async_setup_device_link'), patch.object(integration.mqtt, 'async_publish', new_callable=AsyncMock) as publish:
            assert await integration.async_setup_entry(hass, entry)
            publish.assert_not_awaited()
            assert entry.runtime_data.platforms == [integration.Platform.IMAGE]
            entry.options = {'screenshot_preview_only':False}
            await integration.async_unload_entry(hass, entry)
            hass.config_entries.async_unload_platforms.assert_awaited_once_with(entry, [integration.Platform.IMAGE])
    asyncio.run(run())


@pytest.mark.parametrize("ipv4,failing_ipv4", [(None, False), ("10.0.2.73", True)])
def test_ipv6_preview_and_ipv4_fallback(tmp_path, ipv4, failing_ipv4):
    import json
    from aiohttp import ClientConnectionError

    async def run():
        hass = HomeAssistant(str(tmp_path))
        token = "b" * 32
        entity = WallDisplayScreenshot(hass, WallDisplayRuntime(hass, "panel/test", "Panel", "id"), token)
        entity.hass = hass
        entity.async_write_ha_state = Mock()
        async def chunks(_size):
            yield bmp()
        response = SimpleNamespace(status=200, content=SimpleNamespace(iter_chunked=chunks))
        context = AsyncMock()
        context.__aenter__.return_value = response
        def get(url, **kwargs):
            assert kwargs["headers"] == {"Authorization": "Bearer " + token}
            assert kwargs["allow_redirects"] is False
            if "10.0.2.73" in url and failing_ipv4:
                raise ClientConnectionError("unreachable IPv4")
            return context
        session = SimpleNamespace(get=Mock(side_effect=get))
        async def send(suffix, payload):
            await entity._message(SimpleNamespace(topic="panel/test/"+suffix, payload=payload))
        with patch("custom_components.walldisplay_sync.image.async_get_clientsession", return_value=session):
            await send("status", "online")
            await send("state/device", json.dumps({"ip": ipv4 or "0.0.0.0", "ipv6_global": "fd12:3456::73"}))
            await send("state/screenshot", '{"state":"ready","name":"ipv6","url":"http://evil.example/"}')
            assert entity.available
            assert (await entity.async_image()).startswith(b"\x89PNG")
            assert session.get.call_args.args[0] == "http://[fd12:3456::73]:8080/screenshot.bmp?name=ipv6"
            assert session.get.call_count == (2 if failing_ipv4 else 1)
            # A prefix change invalidates the generation and uses only current hosts.
            await send("state/device", '{"ip":"0.0.0.0","ipv6_global":"fd34::73"}')
            assert session.get.call_args.args[0] == "http://[fd34::73]:8080/screenshot.bmp?name=ipv6"
            requests = session.get.call_count
            await send("state/device", '{"ip":"0.0.0.0","ipv6_global":"2001:4860::73","ipv6_linklocal":"fe80::73"}')
            await send("state/screenshot", '{"state":"ready","name":"blocked"}')
            assert session.get.call_count == requests
            assert token not in str(entity.extra_state_attributes)
    asyncio.run(run())
