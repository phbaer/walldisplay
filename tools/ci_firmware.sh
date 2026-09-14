#!/usr/bin/env bash
# Shared by Forgejo and GitHub. Signing material is provided only to release jobs.
set -euo pipefail
mode="${1:?Expected build or release}"
case "$mode" in
  build) ;;
  release) : "${OTA_SIGNING_KEY_PEM:?Signed releases require OTA_SIGNING_KEY_PEM}" ;;
  *) echo 'Expected build or release' >&2; exit 1 ;;
esac
source "${IDF_PATH:?Activate ESP-IDF or use the ESP-IDF container}/export.sh"
test -f config/panel_config.yaml || cp config/panel_config.example.yaml config/panel_config.yaml
if [ "$mode" = release ]; then
  umask 077
  signing_key_file="$(mktemp /tmp/walldisplay-signing.XXXXXX.pem)"
  trap 'rm -f "$signing_key_file"' EXIT
  printf '%s' "$OTA_SIGNING_KEY_PEM" > "$signing_key_file"
  unset OTA_SIGNING_KEY_PEM
  # Validate the key before spending time compiling. Do not print key material.
  python - "$signing_key_file" <<'PY'
import sys
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
try:
    key = serialization.load_pem_private_key(Path(sys.argv[1]).read_bytes(), password=None)
    assert isinstance(key, rsa.RSAPrivateKey) and key.key_size == 3072
except (ValueError, TypeError, AssertionError):
    raise SystemExit('OTA_SIGNING_KEY_PEM must contain a valid unencrypted RSA-3072 private PEM')
PY
  python tools/build_signed_firmware.py --key "$signing_key_file" --build-dir build
  python tools/check_signed_firmware.py --image build/walldisplay.bin --key "$signing_key_file"
else
  idf.py set-target esp32s3
  idf.py build
fi
sh tests/run_unit_tests.sh
python tools/check_ota_stack.py
