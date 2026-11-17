"""Keep documentation references consistent with the files they describe."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def _documents() -> list[Path]:
    listed = subprocess.run(
        ["git", "ls-files", "-co", "--exclude-standard", "*.md"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    ).stdout.split()
    # ai-prompts are historical generation prompts, not maintained documentation.
    return [ROOT / name for name in listed if not name.startswith("ai-prompts/") and (ROOT / name).exists()]


def _anchor(heading: str) -> str:
    heading = re.sub(r"[`*_]", "", heading.strip().lower())
    return re.sub(r"[^\w\- ]", "", heading).replace(" ", "-")


def test_relative_links_and_anchors_resolve():
    problems = []
    for document in _documents():
        for target in re.findall(r"\]\(([^)\s]+)\)", document.read_text()):
            if re.match(r"[a-z]+://|mailto:", target):
                continue
            path, _, fragment = target.partition("#")
            destination = (document.parent / path).resolve() if path else document
            if not destination.exists():
                problems.append(f"{document.relative_to(ROOT)}: missing {target}")
            elif fragment and destination.suffix == ".md":
                headings = re.findall(r"^#+ (.*)$", destination.read_text(), re.MULTILINE)
                if fragment not in {_anchor(heading) for heading in headings}:
                    problems.append(f"{document.relative_to(ROOT)}: no anchor {target}")
    assert not problems, "\n".join(problems)


def test_readme_names_the_esp_idf_release_container():
    """The README must state the ESP-IDF version that release builds use."""
    workflow = (ROOT / ".forgejo/workflows/release-firmware.yaml").read_text()
    images = set(re.findall(r"image:\s*(espressif/idf:v[0-9.]+)", workflow))
    assert len(images) == 1, f"build and release jobs must use one ESP-IDF image, found {sorted(images)}"
    assert f"`{images.pop()}`" in (ROOT / "README.md").read_text()


def test_moved_reference_is_linked_from_the_readme():
    readme = (ROOT / "README.md").read_text()
    for document in ("docs/README.md", "docs/reference/mqtt-contract.md", "docs/how-to/security-and-signing.md",
                     "docs/how-to/build-and-release.md", "docs/explanation/hardware-and-updates.md"):
        assert f"]({document})" in readme, document
