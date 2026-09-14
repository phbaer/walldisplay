#!/usr/bin/env python3
"""Create or finish one immutable Forgejo firmware release."""
import argparse
import json
import mimetypes
import os
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


class Publisher:
    def __init__(self, server, repo, token):
        self.api = server.rstrip('/') + '/api/v1'
        self.repo = repo
        self.headers = {'Authorization': f'token {token}', 'Accept': 'application/json'}

    def request(self, method, path, body=None, content_type='application/json'):
        headers = dict(self.headers)
        if body is not None:
            headers['Content-Type'] = content_type
        request = Request(self.api + path, data=body, method=method, headers=headers)
        try:
            with urlopen(request, timeout=60) as response:
                payload = response.read()
                return json.loads(payload) if payload else None
        except HTTPError as error:
            if error.code == 404:
                return None
            detail = error.read().decode('utf-8', 'replace')
            raise RuntimeError(f'Forgejo API {method} {path} failed ({error.code}): {detail}') from error

    def release(self, tag, title, notes, prerelease):
        path = f'/repos/{self.repo}/releases/tags/{quote(tag, safe="")}'
        current = self.request('GET', path)
        if current is not None:
            firmware_assets = [asset['name'] for asset in current.get('assets', [])
                               if asset['name'].startswith('walldisplay-')]
            if not current.get('draft') or firmware_assets:
                raise RuntimeError('A published or firmware-populated release already exists; use a new tag')
            return current['id']
        payload = json.dumps({'draft': True, 'name': title, 'body': notes,
                              'prerelease': prerelease, 'tag_name': tag}).encode()
        created = self.request('POST', f'/repos/{self.repo}/releases', payload)
        return created['id']

    def upload(self, release_id, path):
        boundary = 'walldisplay-release-boundary'
        content_type = mimetypes.guess_type(path.name)[0] or 'application/octet-stream'
        data = path.read_bytes()
        prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="attachment"; '
                  f'filename="{path.name}"\r\nContent-Type: {content_type}\r\n\r\n').encode()
        body = prefix + data + f'\r\n--{boundary}--\r\n'.encode()
        self.request('POST', f'/repos/{self.repo}/releases/{release_id}/assets?name={quote(path.name)}',
                     body, f'multipart/form-data; boundary={boundary}')

    def publish(self, release_id):
        self.request('PATCH', f'/repos/{self.repo}/releases/{release_id}',
                     json.dumps({'draft': False}).encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True)
    parser.add_argument('--repo', required=True)
    parser.add_argument('--token', required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('--notes-file', type=Path, required=True)
    parser.add_argument('--prerelease', action='store_true')
    parser.add_argument('--release-dir', type=Path, required=True)
    args = parser.parse_args()
    publisher = Publisher(args.server, args.repo, args.token)
    release_id = publisher.release(args.tag, args.title, args.notes_file.read_text(), args.prerelease)
    for path in sorted(args.release_dir.iterdir()):
        if path.is_file():
            publisher.upload(release_id, path)
    publisher.publish(release_id)
    print(f'Published Forgejo release {args.tag}')


if __name__ == '__main__':
    main()
