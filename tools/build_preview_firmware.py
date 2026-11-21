#!/usr/bin/env python3
"""Build an unsigned OTA image with signature verification enabled."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--version", required=True, help="Firmware version to embed")
parser.add_argument("--build-dir", type=Path, default=Path("build-preview"))
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9.+-]{1,32}", args.version):
    raise SystemExit("--version must contain 1–32 ASCII version characters")

root = Path(__file__).resolve().parents[1]
cache = root / ".cache/preview-build"
cache.mkdir(parents=True, exist_ok=True)
sdkconfig = cache / "sdkconfig"
if sdkconfig.exists():
    sdkconfig.unlink()
command = [
    "idf.py", "-B", str(args.build_dir), "-DIDF_TARGET=esp32s3",
    f"-DAPP_FW_VERSION_OVERRIDE={args.version}", f"-DSDKCONFIG={sdkconfig}",
    f"-DSDKCONFIG_DEFAULTS={root / 'sdkconfig.defaults'};{root / 'config/sdkconfig.signed-ota'};{root / 'config/sdkconfig.preview-ota'}",
]
subprocess.run([*command, "reconfigure"], cwd=root, check=True)
settings = set(sdkconfig.read_text().splitlines())
required = {
    'CONFIG_IDF_TARGET="esp32s3"',
    "CONFIG_SECURE_SIGNED_ON_UPDATE=y",
    "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y",
    "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=n",
}
if not required <= settings or "CONFIG_SECURE_BOOT=y" in settings:
    raise SystemExit("Preview build configuration does not match unsigned OTA policy")
subprocess.run([*command, "build"], cwd=root, check=True)
if not (args.build_dir / "walldisplay.bin").is_file():
    raise SystemExit("ESP-IDF did not produce the expected application image")
