"""Release tags, source identity, artifact contents and overwrite protection."""
import hashlib
import importlib.util
import json
import os
import io
from pathlib import Path
import tempfile
import tarfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

spec = importlib.util.spec_from_file_location('firmware_release', Path(__file__).resolve().parents[1] / 'tools/firmware_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class FirmwareReleaseTests(unittest.TestCase):
    def test_tag_validation(self):
        for tag in ('v1.0.0', 'v1.0.0-rc.1', 'v1.2.0-beta.12', 'v0.0.1-alpha'):
            self.assertEqual(release.validate_tag(tag), tag)
        for tag in ('main', 'v1', 'v01.0.0', 'v1.0.0-01', 'v1.0.0;echo bad', 'v1.0.0\n', 'v1.0.0-' + 'x' * 30):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate_tag(tag)

    def test_manual_tag_must_match_checkout(self):
        with patch.object(release, 'git', side_effect=['a' * 40, 'b' * 40]):
            with self.assertRaises(ValueError):
                release.select('v1.0.0-rc.1')
        with patch.object(release, 'git', return_value='b' * 40):
            self.assertEqual(release.select('v1.0.0-rc.1')['sha'], 'b' * 40)
            self.assertEqual(release.select()['version'], 'dev-' + 'b' * 12)
            self.assertEqual(release.select('v1.0.0')['prerelease'], 'false')

    def test_packages_final_binary_and_checkout_sha(self):
        with tempfile.TemporaryDirectory() as temp:
            old = Path.cwd()
            try:
                os.chdir(temp)
                build = Path('build')
                for filename in ('walldisplay.bin', 'bootloader/bootloader.bin', 'partition_table/partition-table.bin', 'ota_data_initial.bin'):
                    path = build / filename
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'final signed image')
                Path('partitions.csv').write_text('example')
                with patch.object(release, 'git', return_value='actual-tag-sha'):
                    with patch.object(release.subprocess, 'run') as merge:
                        def create_factory(command, check):
                            Path(command[command.index('--output') + 1]).write_bytes(b'factory image')
                        merge.side_effect = create_factory
                        with patch.object(release.shutil, 'which', return_value='esptool'):
                            release.package('v1.0.0-rc.1', 'https://forge.example', 'owner/repo')
                            release.package('dev-123', '', '', dist=Path('dev-dist'))
                        self.assertEqual(merge.call_count, 2)
                        self.assertIn('merge-bin', merge.call_args_list[0].args[0])
                metadata = json.loads(Path('dist/walldisplay-v1.0.0-rc.1.json').read_text())
                self.assertEqual(metadata['build'], 'actual-tag-sha')
                self.assertEqual(metadata['sha256'], hashlib.sha256(b'final signed image').hexdigest())
                self.assertEqual(metadata['size'], len(b'final signed image'))
                self.assertIn('/releases/download/v1.0.0-rc.1/', metadata['url'])
                dev = json.loads(Path('dev-dist/walldisplay-dev-123-build-info.json').read_text())
                self.assertNotIn('url', dev)
                self.assertEqual(Path('dist/walldisplay-v1.0.0-rc.1-factory.bin').read_bytes(), b'factory image')
                with tarfile.open('dist/walldisplay-v1.0.0-rc.1-factory.tar.gz') as archive:
                    self.assertEqual(len(archive.getnames()), 5)
                    self.assertIn('ota_data_initial.bin', archive.getnames())
                with self.assertRaises(FileExistsError):
                    release.package('v1.0.0', '', '')
            finally:
                os.chdir(old)

    def test_hacs_can_coexist_but_firmware_cannot_be_replaced(self):
        release.check_existing({'assets': [{'name': 'walldisplay_sync.zip'}]})
        for existing in ({'draft': True}, {'assets': [{'name': 'walldisplay-v1.0.0.bin'}]}):
            with self.assertRaises(ValueError):
                release.check_existing(existing)

    def test_release_lookup_only_tolerates_not_found(self):
        with patch.dict(os.environ, {'RELEASE_TOKEN': 'test-token'}):
            for platform in ('forgejo', 'github'):
                with patch.object(release, 'urlopen', side_effect=HTTPError('url', 404, 'missing', {}, None)):
                    release.preflight(platform, 'https://forge.example', 'owner/repo', 'v1.0.0')
                for code in (401, 403, 500):
                    with patch.object(release, 'urlopen', side_effect=HTTPError('url', code, 'error', {}, None)):
                        with self.assertRaises(HTTPError):
                            release.preflight(platform, 'https://forge.example', 'owner/repo', 'v1.0.0')
                with patch.object(release, 'urlopen', return_value=io.BytesIO(b'{"assets":[{"name":"walldisplay-v1.0.0.bin"}]}')):
                    with self.assertRaises(ValueError):
                        release.preflight(platform, 'https://forge.example', 'owner/repo', 'v1.0.0')

    def test_preflight_recovers_empty_draft(self):
        release_json = b'{"id":42,"draft":true,"assets":[]}'
        with patch.dict(os.environ, {'RELEASE_TOKEN': 'test-token'}):
            with patch.object(release, 'urlopen', side_effect=[io.BytesIO(release_json), io.BytesIO(b'')]) as opened:
                release.preflight('forgejo', 'https://forge.example', 'owner/repo', 'v1.0.0-rc.1', True)
                self.assertEqual(opened.call_count, 2)
                self.assertEqual(opened.call_args_list[1].args[0].method, 'DELETE')

    def test_preflight_does_not_delete_draft_with_firmware(self):
        release_json = b'{"id":42,"draft":true,"assets":[{"name":"walldisplay-v1.0.0-rc.1.bin"}]}'
        with patch.dict(os.environ, {'RELEASE_TOKEN': 'test-token'}):
            with patch.object(release, 'urlopen', return_value=io.BytesIO(release_json)):
                with self.assertRaises(ValueError):
                    release.preflight('forgejo', 'https://forge.example', 'owner/repo', 'v1.0.0-rc.1', True)


if __name__ == '__main__':
    unittest.main()
