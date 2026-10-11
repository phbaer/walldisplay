"""Authenticated panel screenshot preview, proxied through Home Assistant."""
from __future__ import annotations

import asyncio
from io import BytesIO
from ipaddress import ip_address
import json
import re

from aiohttp import ClientError, ClientTimeout
from PIL import Image
from homeassistant.components import mqtt
from homeassistant.components.image import ImageEntity
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.util import dt as dt_util

from .configuration import migrate_configuration

BMP_BYTES = 54 + 480 * 480 * 3


def _png(data: bytes) -> bytes:
    if (len(data) != BMP_BYTES or data[:2] != b"BM" or
        int.from_bytes(data[18:22], "little") != 480 or int.from_bytes(data[22:26], "little") != 480):
        raise ValueError("Invalid screenshot")
    with Image.open(BytesIO(data)) as bmp:
        if bmp.size != (480, 480) or bmp.format != "BMP":
            raise ValueError("Invalid screenshot")
        output = BytesIO()
        bmp.save(output, format="PNG")
        return output.getvalue()


def _host(value: str) -> str | None:
    try:
        ip = ip_address(value)
    except ValueError:
        return None
    if not ip.is_private or ip.is_loopback or ip.is_unspecified or ip.is_multicast:
        return None
    return f"[{ip}]" if ip.version == 6 else str(ip)


async def async_setup_entry(hass, entry, async_add_entities) -> None:
    config = migrate_configuration({**entry.data, **entry.options})
    entity = WallDisplayScreenshot(hass, entry.runtime_data, config["screenshot_token"])
    async_add_entities([entity])


class WallDisplayScreenshot(ImageEntity):
    _attr_has_entity_name = True
    _attr_name = "Panel Screenshot"
    _attr_content_type = "image/png"

    def __init__(self, hass, runtime, token: str) -> None:
        super().__init__(hass)
        self._runtime = runtime
        self._token = token
        self._host = None
        self._name = None
        self._image = None
        self._online = False
        self._generation = 0
        self._fetched_generation = -1
        self._attr_unique_id = f"{runtime.identifier}_screenshot_image"
        self._attr_available = False
        self._attr_extra_state_attributes = {"capture_state": "unknown", "preview_error": "token_missing" if not token else None}

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        for suffix in ("status", "state/device", "state/screenshot"):
            self.async_on_remove(await mqtt.async_subscribe(
                self.hass, f"{self._runtime.topic}/{suffix}", self._message, 1
            ))

    async def _message(self, msg) -> None:
        if msg.topic.endswith("/status"):
            self._online = msg.payload == "online"
            if not self._online:
                self._generation += 1
                self._name = None
                self._image = None
                self._attr_available = False
                self.async_write_ha_state()
                return
        else:
            try:
                data = json.loads(msg.payload)
                if not isinstance(data, dict):
                    return
            except (ValueError, TypeError):
                return
            if msg.topic.endswith("/state/device"):
                host = _host(str(data.get("ip", "")))
                if host != self._host:
                    self._generation += 1
                    self._host = host
            else:
                self._generation += 1
                state = data.get("state", "unknown")
                self._name = data.get("name") if state == "ready" else None
                self._attr_extra_state_attributes = {
                    "capture_state": state, "capture_name": data.get("name"),
                    "capture_error": data.get("error"), "capture_stage": data.get("stage"),
                    "preview_error": "token_missing" if not self._token else None,
                }
                self.async_write_ha_state()
        if self._generation != self._fetched_generation and self._online and self._host and self._token and isinstance(self._name, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,31}", self._name):
            await self._fetch(self._generation)

    async def _fetch(self, generation: int) -> None:
        url = f"http://{self._host}:8080/screenshot.bmp?name={self._name}"
        try:
            async with async_get_clientsession(self.hass).get(
                url, headers={"Authorization": f"Bearer {self._token}"},
                timeout=ClientTimeout(total=30), allow_redirects=False,
            ) as response:
                if response.status != 200:
                    raise ValueError(f"http_{response.status}")
                data = bytearray()
                async for chunk in response.content.iter_chunked(16384):
                    data.extend(chunk)
                    if len(data) > BMP_BYTES:
                        raise ValueError("invalid_image")
            png = await self.hass.async_add_executor_job(_png, bytes(data))
        except (ClientError, asyncio.TimeoutError, ValueError, OSError) as error:
            if generation != self._generation:
                return
            # Never include request headers/token or an arbitrary response body in entity state.
            self._attr_extra_state_attributes["preview_error"] = str(error) if isinstance(error, ValueError) and str(error).startswith("http_") else "download_failed"
            self.async_write_ha_state()
            return
        if generation != self._generation or not self._online:
            return
        self._fetched_generation = generation
        self._image = png
        self._attr_available = True
        self._attr_image_last_updated = dt_util.utcnow()
        self._attr_extra_state_attributes["preview_error"] = None
        self.async_write_ha_state()

    async def async_image(self) -> bytes | None:
        return self._image
