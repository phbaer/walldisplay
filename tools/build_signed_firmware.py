#!/usr/bin/env python3
"""Build signed firmware with an external RSA-3072 key (no eFuse changes)."""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--key", type=Path, required=True, help="External RSA-3072 private PEM key")
parser.add_argument("--build-dir", type=Path, default=Path("build-signed"))
parser.add_argument("--version", help="Optional tagged firmware version to embed in the image")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
if args.version and not re.fullmatch(r"[A-Za-z0-9.+-]{1,32}", args.version):
    raise SystemExit("--version must contain 1–32 ASCII version characters")
key = args.key.resolve(strict=True)
cache = root / ".cache/signing"
cache.mkdir(parents=True, exist_ok=True)
defaults = cache / "key.defaults"
defaults.write_text(f"CONFIG_SECURE_BOOT_SIGNING_KEY={json.dumps(str(key))}\n")
# Each run regenerates settings from committed defaults, never from an insecure
# sdkconfig left in another build directory. The key itself stays outside the repo.
sdkconfig = cache / "sdkconfig"
if sdkconfig.exists():
    sdkconfig.unlink()
command = [
    "idf.py", "-B", str(args.build_dir), "-DIDF_TARGET=esp32s3", f"-DSDKCONFIG={sdkconfig}",
    f"-DSDKCONFIG_DEFAULTS={root / 'sdkconfig.defaults'};{root / 'config/sdkconfig.signed-ota'};{defaults}",
]
if args.version:
    command.insert(4, f"-DAPP_FW_VERSION_OVERRIDE={args.version}")
# A clean CI runner otherwise defaults to ESP32 and selects ECDSA v1 signing.
# Check the generated policy before compiling or signing any artifacts.
subprocess.run([*command, "reconfigure"], cwd=root, check=True)
config = sdkconfig.read_text()
required = {"CONFIG_IDF_TARGET=\"esp32s3\"", "CONFIG_SECURE_SIGNED_ON_UPDATE=y",
            "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y", "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y"}
settings = set(config.splitlines())
if not required <= settings or "CONFIG_SECURE_BOOT=y" in settings:
    raise SystemExit("Signing configuration did not match the required OTA-only policy")
subprocess.run([*command, "build"], cwd=root, check=True)
