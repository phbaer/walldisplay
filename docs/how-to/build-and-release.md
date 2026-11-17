# Test builds and releases

How firmware and integration builds, tags, and releases work on Forgejo and GitHub. Moved unchanged from the README section "Test builds and releases"; the [README](../../README.md) remains the overview.

The GitHub HACS workflows pin `hacs/action` to commit `d556e736723344f83838d08488c983a15381059a` (`22.5.0`) while its moving `main` action has a manifest-discovery regression that can report valid manifests as `None`. This repository is mirrored from Forgejo to GitHub. For every `v*` tag received by GitHub, GitHub publishes the Home Assistant integration ZIP: an exact `vMAJOR.MINOR.PATCH` tag creates a normal latest release, while a tag with a prerelease suffix such as `-beta.1` or `-rc.1` creates a prerelease. Non-default branch pushes have separate short-lived `v<project-version>b<id>` preview releases; the base version is read from `pyproject.toml`, so preview tags stay aligned with the canonical project version. Tags are explicitly excluded from that workflow, so an RC tag produces only its matching tagged release. These GitHub releases contain the integration only; Forgejo remains the source for signed firmware releases. The workflows use the GitHub CLI to create a release only when it is missing and upload the integration ZIP separately, so rerunning a tag never tries to rewrite an existing release's target commit. No release branches are required.

When repairing an existing tag, run the HACS workflow manually from a branch containing the current workflow and pass that branch/commit as `ref` together with the existing release tag as `tag`. A tag-triggered run uses the workflow file stored at that tag, so an older tag cannot automatically pick up later workflow fixes. The GitHub Actions workflow must have `contents: write`; if the repository or organization defaults to read-only tokens, enable read/write workflow permissions in GitHub Actions settings.

Before running a Forgejo firmware release, create a repository **Actions variable** named `PUBLIC_URL` containing `https://git.baer.one` (without a trailing `/`). Forgejo reserves names beginning with `FORGEJO_`, so use `PUBLIC_URL` exactly. The workflow uses this value because `forgejo.server_url` can resolve to an internal Docker hostname; it is used for release API access and embedded in OTA manifest download links.

Forgejo is the primary source for firmware builds, signed releases, and OTA assets. The [Forgejo firmware workflow](../../.forgejo/workflows/release-firmware.yaml) uses the shared build, validation, and packaging scripts. GitHub firmware building and publishing are disabled; do not configure `OTA_SIGNING_KEY_PEM` on GitHub. Its HACS integration releases and integration-only branch previews remain active. After checkout, each job adds its exact workspace path to Git’s `safe.directory` configuration inside the build container, allowing Git commands against the runner-owned checkout without trusting arbitrary directories.

Build behavior:

| Trigger | Result | Signing |
| --- | --- | --- |
| Branch push, PR, or manual run with empty `release_tag` | `dev-<checked-out SHA>` workflow artifacts, retained for 14 days | Unsigned; remote OTA disabled |
| Tag such as `v1.0.0-beta.1` or `v1.0.0-rc.1` | Prerelease with update image, merged factory image, partition archive, changelog, and OTA manifest | Required |
| Tag such as `v1.0.0` | Stable release with the same assets | Required |

To publish a test build from the current commit:

```sh
git tag -a v1.0.0-rc.1 -m "First release candidate"
git push origin v1.0.0-rc.1
```

Push the tag to your Forgejo remote to publish firmware (`origin` in the example above). The Forgejo mirror must mirror `refs/tags/v*`; once GitHub receives a new tag, its integration workflow publishes the matching GitHub release automatically. Do not create or retain release branches. Normal non-default branches continue to publish short-lived HACS branch previews through the separate branch-preview workflow; those previews are removed when the branch is deleted.

If a tag was already mirrored before the workflow commit, no new GitHub push event occurs. A maintainer can manually run the HACS workflow with `ref` set to the tagged commit and `tag` set to the existing release tag; this is also the recovery path for an existing release. The firmware workflow verifies that its checked-out commit matches an exact tag. Tags use `vMAJOR.MINOR.PATCH` with an optional SemVer prerelease suffix and must fit the firmware's 32-character manifest version limit. The checked-in project and blueprint version remains the canonical `1.3.2`, while each tagged firmware build embeds its exact tag (for example `v1.0.0-rc.8`) in `APP_FW_VERSION`. Home Assistant can therefore order successive release candidates correctly; the stable `v1.0.0` image reports the stable version and is not offered an older RC.

Download ordinary test packages from the workflow run's artifacts. They contain the update image, merged factory image, partition archive, and build metadata; they deliberately have no OTA manifest pointing at a nonexistent release. Published manifests contain the checked-out commit SHA and the hash and size of the final signed binary. To test OTA, explicitly select the prerelease manifest URL and use the key already trusted by the panel. A prerelease label does not restrict firmware installation.

Publish a new `rc.2`, `rc.3`, etc. for each revision. Firmware publishing refuses existing firmware assets and never overwrites them; protect `v*` tags against unauthorized creation, movement, and deletion in repository settings. Restrict manual release runs and changes to release workflows to trusted maintainers. Use isolated runners for untrusted PR builds, especially on self-hosted Forgejo. Job conditions separate routine builds from signing but do not replace host-level access controls. Keep the firmware signing secret on Forgejo only.

Forgejo updates the wiki changelog only for stable firmware releases; each prerelease still includes its own changelog. The publisher reuses an existing empty draft release, which recovers a failure before the first asset upload. A published release or a release containing firmware assets is never deleted or overwritten; use a new tag for those cases. No automatic promotion or rolling test OTA channel is configured.

Forgejo uses its supported `upload-artifact@v3` action; see [Forgejo artifact compatibility](https://forgejo.org/docs/v15.0/user/actions/advanced-features/). Run `python -m unittest discover -s tests -p 'test_firmware_release.py'` to check tag validation, source identity, packaging, and overwrite protection locally.
