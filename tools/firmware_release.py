#!/usr/bin/env python3
"""Shared CI release selection, firmware packaging, and overwrite protection."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def validate_tag(tag):
    number = r'(?:0|[1-9][0-9]*)'
    identifier = r'(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
    if len(tag) > 32 or not re.fullmatch(
        rf'v{number}\.{number}\.{number}(?:-{identifier}(?:\.{identifier})*)?', tag
    ):
        raise ValueError('Use a vMAJOR.MINOR.PATCH[-prerelease] tag of at most 32 characters')
    return tag


def select(tag=''):
    sha = git('rev-parse', 'HEAD')
    if tag:
        validate_tag(tag)
        if git('rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}') != sha:
            raise ValueError('The requested existing tag must match the checked-out commit')
    return {'version': tag or f'dev-{sha[:12]}', 'sha': sha,
            'prerelease': str(bool(tag and '-' in tag)).lower()}


def package(version, server, repo, build=Path('build'), dist=Path('dist')):
    # Never mix artifacts from different builds or accidentally publish stale files.
    dist.mkdir(exist_ok=False)
    image = build / 'walldisplay.bin'
    name = f'walldisplay-{version}'
    shutil.copyfile(image, dist / f'{name}.bin')
    factory = dist / f'{name}-factory.bin'
    esptool = shutil.which('esptool')
    if esptool is None:
        raise RuntimeError('esptool executable is required to create the merged factory image')
    subprocess.run([
        esptool, '--chip', 'esp32s3', 'merge-bin',
        '--output', str(factory), '--flash-mode', 'dio', '--flash-freq', '80m',
        '--flash-size', 'keep',
        '0x0', str(build / 'bootloader/bootloader.bin'),
        '0x8000', str(build / 'partition_table/partition-table.bin'),
        '0x14000', str(build / 'ota_data_initial.bin'),
        '0x30000', str(image),
    ], check=True)
    with tarfile.open(dist / f'{name}-factory.tar.gz', 'w:gz') as archive:
        for source, target in [(image, 'walldisplay.bin'),
                               (build / 'bootloader/bootloader.bin', 'bootloader.bin'),
                               (build / 'partition_table/partition-table.bin', 'partition-table.bin'),
                               (build / 'ota_data_initial.bin', 'ota_data_initial.bin'),
                               (Path('partitions.csv'), 'partitions.csv')]:
            archive.add(source, arcname=target)
    metadata = {'version': version, 'target': 'esp32s3',
                'sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
                'size': image.stat().st_size, 'build': git('rev-parse', 'HEAD')}
    if version.startswith('v'):
        validate_tag(version)
        metadata['url'] = f'{server.rstrip("/")}/{repo}/releases/download/{version}/{name}.bin'
        filename = f'{name}.json'
    else:
        # Build-only artifacts have no public OTA download URL.
        filename = f'{name}-build-info.json'
    (dist / filename).write_text(json.dumps(metadata, indent=2) + '\n')


def check_existing(release):
    if release.get('draft') or any(
        asset['name'].startswith('walldisplay-') for asset in release.get('assets', [])
    ):
        raise ValueError('Release already contains firmware or is a draft; publish a new tag')


def preflight(platform, server, repo, tag, recover_draft=False):
    validate_tag(tag)
    api = os.environ.get('GITHUB_API_URL', 'https://api.github.com') if platform == 'github' else server.rstrip('/') + '/api/v1'
    request = Request(f'{api}/repos/{repo}/releases/tags/{quote(tag, safe="")}',
                      headers={'Authorization': f'Bearer {os.environ["RELEASE_TOKEN"]}',
                               'Accept': 'application/json'})
    try:
        with urlopen(request, timeout=30) as response:
            release = json.load(response)
    except HTTPError as error:
        if error.code == 404:
            return
        raise
    # Recover only a draft with no firmware assets. This is safe after a failed
    # publisher run created the release before uploading its first asset.
    if recover_draft and release.get('draft') and not any(
        asset['name'].startswith('walldisplay-') for asset in release.get('assets', [])
    ):
        delete_request = Request(
            f'{api}/repos/{repo}/releases/{release["id"]}',
            method='DELETE',
            headers={'Authorization': f'Bearer {os.environ["RELEASE_TOKEN"]}',
                     'Accept': 'application/json'},
        )
        with urlopen(delete_request, timeout=30):
            pass
        return
    # HACS may already have created this release; its distinct ZIP can coexist.
    check_existing(release)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['select', 'package', 'preflight'])
    parser.add_argument('--tag', default='')
    parser.add_argument('--version', default='')
    parser.add_argument('--platform', choices=['forgejo', 'github'])
    parser.add_argument('--server', default='')
    parser.add_argument('--repo', default='')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--recover-draft', action='store_true')
    args = parser.parse_args()
    if args.action == 'select':
        values = select(args.tag)
        with args.output.open('a') as output:
            for key, value in values.items():
                output.write(f'{key}={value}\n')
    elif args.action == 'package':
        package(args.version, args.server, args.repo)
    else:
        preflight(args.platform, args.server, args.repo, args.tag, args.recover_draft)


if __name__ == '__main__':
    main()
