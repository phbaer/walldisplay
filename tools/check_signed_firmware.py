#!/usr/bin/env python3
"""Verify a signed image, then prove tampering and a different signer are rejected."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--image", type=Path, required=True)
parser.add_argument("--key", type=Path, required=True)
args = parser.parse_args()
def verify(image, key):
    return subprocess.run([sys.executable, "-m", "espsecure", "verify-signature", "--version", "2", "--keyfile", str(key), str(image)], capture_output=True).returncode == 0
if not verify(args.image, args.key):
    raise SystemExit("Image signature is not valid for the supplied key")
with tempfile.TemporaryDirectory(prefix="walldisplay-signature-test-") as folder:
    altered = Path(folder) / "altered.bin"
    data = bytearray(args.image.read_bytes())
    data[64] ^= 1
    altered.write_bytes(data)
    if verify(altered, args.key):
        raise SystemExit("Tampered firmware was accepted")
    other = Path(folder) / "other.pem"
    subprocess.run([sys.executable, "-m", "espsecure", "generate-signing-key", "--version", "2", str(other)], check=True, capture_output=True)
    if verify(args.image, other):
        raise SystemExit("Firmware was accepted with a different signer")
print("Signature valid; tampered firmware and wrong signer rejected")
