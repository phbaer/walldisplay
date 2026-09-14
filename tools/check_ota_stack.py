#!/usr/bin/env python3
"""Guard OTA's direct stack frames; runtime logs measure deeper HTTP/TLS usage."""
import argparse
from pathlib import Path
import re
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=Path, default=Path("build"))
parser.add_argument("--objdump", default="xtensa-esp32s3-elf-objdump")
args = parser.parse_args()
obj = args.build_dir / "esp-idf/src/CMakeFiles/__idf_src.dir/ota_manager.c.obj"
disassembly = subprocess.check_output([args.objdump, "-d", str(obj)], text=True)
frames = {}
for name in ("ota_task", "download_firmware"):
    match = re.search(r"<" + name + r">:\s*\n[^\n]*\bentry\s+a1,\s*(0x[0-9a-f]+|\d+)", disassembly)
    if not match:
        raise SystemExit(f"Cannot inspect stack frame for {name}")
    frames[name] = int(match[1], 0)
total = sum(frames.values())
if total > 2048:
    raise SystemExit(f"OTA direct stack frames exceed 2048-byte budget: {frames}")
print(f"OTA direct stack frames: {frames}; combined {total} bytes (budget 2048)")
