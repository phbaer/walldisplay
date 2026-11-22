#!/usr/bin/env python3
"""Remove signed preview releases and their tags for a closed pull request."""
import json
import os
import re
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


def main():
    pr = int(os.environ["PR_NUMBER"])
    base = f'{os.environ["SERVER"].rstrip("/")}/api/v1/repos/{os.environ["REPOSITORY"]}'
    headers = {"Authorization": f'Bearer {os.environ["RELEASE_TOKEN"]}'}

    def request(path, method="GET"):
        try:
            with urlopen(Request(base + path, headers=headers, method=method), timeout=30) as response:
                return json.load(response) if method == "GET" else None
        except HTTPError as error:
            if method == "DELETE" and error.code == 404:
                return None
            raise

    def pages(path):
        for page in range(1, 1001):
            entries = request(f"{path}?limit=50&page={page}")
            if not entries:
                return
            yield from entries
        raise RuntimeError(f"Pagination limit exceeded for {path}")

    if request(f"/pulls/{pr}")["state"] != "closed":
        return

    pattern = re.compile(rf"v[0-9]+\.[0-9]+\.[0-9]+-pr\.{pr}\.[0-9]+\.[0-9]+")
    releases = {release["tag_name"]: release for release in pages("/releases")
                if pattern.fullmatch(release["tag_name"])}
    tags = {tag["name"] for tag in pages("/tags") if pattern.fullmatch(tag["name"])}
    for tag in sorted(tags | releases.keys()):
        if request(f"/pulls/{pr}")["state"] != "closed":
            return
        release = releases.get(tag)
        if release:
            if not (release["draft"] or release["prerelease"]):
                raise RuntimeError(f"Refusing to delete a regular release: {tag}")
            request(f'/releases/{release["id"]}', "DELETE")
        request(f"/tags/{quote(tag, safe='')}", "DELETE")
        print(f"Removed preview {tag}")


if __name__ == "__main__":
    main()
