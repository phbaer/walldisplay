import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'publish_forgejo_release', Path(__file__).resolve().parents[1] / 'tools/publish_forgejo_release.py')
publisher_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher_module)


class PublishForgejoReleaseTests(unittest.TestCase):
    def test_reuses_empty_draft_and_uploads_then_publishes(self):
        publisher = publisher_module.Publisher('https://git.baer.one', 'owner/repo', 'token')
        requests = []

        def request(method, path, body=None, content_type='application/json'):
            requests.append((method, path, body, content_type))
            if method == 'GET':
                return {'id': 7, 'draft': True, 'assets': [{'name': 'walldisplay_sync.zip'}]}
            return None

        publisher.request = request
        with tempfile.TemporaryDirectory() as temp:
            asset = Path(temp) / 'walldisplay-v1.0.0-rc.1.bin'
            asset.write_bytes(b'signed')
            release_id = publisher.release('v1.0.0-rc.1', 'Title', 'Notes', True)
            publisher.upload(release_id, asset)
            publisher.publish(release_id)
        self.assertEqual(release_id, 7)
        self.assertEqual([item[0] for item in requests], ['GET', 'POST', 'PATCH'])
        self.assertIn('multipart/form-data', requests[1][3])

    def test_rejects_published_or_firmware_populated_release(self):
        publisher = publisher_module.Publisher('https://git.baer.one', 'owner/repo', 'token')
        for release in ({'id': 7, 'draft': False, 'assets': []},
                        {'id': 7, 'draft': True, 'assets': [{'name': 'walldisplay-v1.0.0-rc.1.bin'}]}):
            with patch.object(publisher, 'request', return_value=release):
                with self.assertRaises(RuntimeError):
                    publisher.release('v1.0.0-rc.1', 'Title', 'Notes', True)


if __name__ == '__main__':
    unittest.main()
