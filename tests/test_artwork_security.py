"""Exercise HTTP limits, retries and competing artwork downloads without network."""
import asyncio
from contextlib import asynccontextmanager
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch
from homeassistant.core import HomeAssistant  # Initialize HA validation before other imports.
from aiohttp import ClientError
from custom_components.walldisplay_sync.artwork import ArtworkCache, _jpeg


def response(chunks, length=None):
    async def iterate(_):
        for chunk in chunks:
            yield chunk
    return SimpleNamespace(content_length=length, raise_for_status=lambda: None,
                           content=SimpleNamespace(iter_chunked=iterate))


class ArtworkTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.hass = SimpleNamespace(async_add_executor_job=AsyncMock(side_effect=lambda f, data: b"jpeg:" + data))
        self.cache = ArtworkCache(self.hass, "panel")

    async def test_bounded_stream_without_content_length(self):
        @asynccontextmanager
        async def get(_):
            yield response([b"123", b"456"])
        with patch("custom_components.walldisplay_sync.artwork.async_get_clientsession", return_value=SimpleNamespace(get=get)), patch("custom_components.walldisplay_sync.artwork.MAX_ARTWORK_BYTES", 5):
            self.assertFalse(await self.cache.async_update("http://ha/image"))
        self.hass.async_add_executor_job.assert_not_called()

    async def test_oversize_content_length(self):
        @asynccontextmanager
        async def get(_):
            yield response([], 2 * 1024 * 1024)
        with patch("custom_components.walldisplay_sync.artwork.async_get_clientsession", return_value=SimpleNamespace(get=get)):
            self.assertFalse(await self.cache.async_update("http://ha/image"))
        self.hass.async_add_executor_job.assert_not_called()

    async def test_failed_source_is_retryable_and_revision_changes(self):
        attempts = 0
        @asynccontextmanager
        async def get(url):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise ClientError("temporary error")
            yield response([url.encode()])
        with patch("custom_components.walldisplay_sync.artwork.async_get_clientsession", return_value=SimpleNamespace(get=get)):
            self.assertFalse(await self.cache.async_update("http://ha/one"))
            self.assertTrue(await self.cache.async_update("http://ha/one"))
            first = self.cache.url("http://ha")
            self.assertTrue(await self.cache.async_update("http://ha/two"))
            self.assertNotEqual(first, self.cache.url("http://ha"))

    async def test_old_download_cannot_replace_new_image(self):
        started, release = asyncio.Event(), asyncio.Event()
        @asynccontextmanager
        async def get(url):
            if url.endswith("old"):
                started.set()
                await release.wait()
            yield response([url.encode()])
        with patch("custom_components.walldisplay_sync.artwork.async_get_clientsession", return_value=SimpleNamespace(get=get)):
            old = asyncio.create_task(self.cache.async_update("http://ha/old"))
            await started.wait()
            self.assertTrue(await self.cache.async_update("http://ha/new"))
            release.set()
            self.assertFalse(await old)
            self.assertEqual(self.cache.jpeg, b"jpeg:http://ha/new")

    async def test_clear_cancels_inflight_result(self):
        started, release = asyncio.Event(), asyncio.Event()
        @asynccontextmanager
        async def get(_):
            started.set()
            await release.wait()
            yield response([b"old"])
        with patch("custom_components.walldisplay_sync.artwork.async_get_clientsession", return_value=SimpleNamespace(get=get)):
            old = asyncio.create_task(self.cache.async_update("http://ha/old"))
            await started.wait()
            await self.cache.async_update("")
            release.set()
            self.assertFalse(await old)
            self.assertIsNone(self.cache.jpeg)

    async def test_url_validation(self):
        for url in ("file:///etc/passwd", "http://user:password@host/image", "ftp://host/image"):
            self.assertFalse(await self.cache.async_update(url))

    def test_image_dimension_limit(self):
        from io import BytesIO
        from PIL import Image
        data = BytesIO()
        Image.new("RGB", (3, 3)).save(data, "PNG")
        with patch("custom_components.walldisplay_sync.artwork.MAX_ARTWORK_PIXELS", 4), self.assertRaises(ValueError):
            _jpeg(data.getvalue())
