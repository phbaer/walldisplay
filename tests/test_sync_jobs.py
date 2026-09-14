import asyncio
import unittest
from custom_components.walldisplay_sync.sync_jobs import publish_independently

class SyncJobsTests(unittest.IsolatedAsyncioTestCase):
    async def test_slow_and_failed_sources_do_not_block_controls(self):
        slow_started, release, controls_ready = asyncio.Event(), asyncio.Event(), asyncio.Event()
        async def slow_artwork():
            slow_started.set()
            await release.wait()
        async def failed_weather():
            raise ValueError("unavailable source")
        async def controls():
            controls_ready.set()
        with self.assertLogs("custom_components.walldisplay_sync.sync_jobs", level="ERROR"):
            task = asyncio.create_task(publish_independently(slow_artwork, failed_weather, controls))
            await asyncio.wait_for(slow_started.wait(), 1)
            await asyncio.wait_for(controls_ready.wait(), 1)
            self.assertFalse(task.done())
            release.set()
            await task
