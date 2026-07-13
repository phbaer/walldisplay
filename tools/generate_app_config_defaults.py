#!/usr/bin/env python3
"""Generate C header with app defaults from a simple YAML file."""

from __future__ import annotations

import sys
from pathlib import Path


REQUIRED_KEYS = [
    ("wifi", "ssid"),
    ("wifi", "password"),
    ("mqtt", "uri"),
    ("mqtt", "username"),
    ("mqtt", "password"),
    ("homeassistant", "discovery_prefix"),
    ("homeassistant", "base_topic"),
    ("homeassistant", "enable_discovery"),
]


def parse_value(raw: str):
    value = raw.strip()
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1]

    lower = value.lower()
    if lower in ("true", "false"):
        return lower == "true"

    return value


def parse_simple_yaml(path: Path):
    data = {}
    section = None

    for idx, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip()

        if not stripped or stripped.startswith("#"):
            continue

        if not line.startswith(" "):
            if not stripped.endswith(":"):
                raise ValueError(f"{path}:{idx}: expected top-level section ending with ':'")
            section = stripped[:-1].strip()
            if not section:
                raise ValueError(f"{path}:{idx}: empty section name")
            data.setdefault(section, {})
            continue

        if section is None:
            raise ValueError(f"{path}:{idx}: found nested key before any section")

        if not line.startswith("  "):
            raise ValueError(f"{path}:{idx}: expected two-space indentation for keys")

        key_line = line[2:].strip()
        if ":" not in key_line:
            raise ValueError(f"{path}:{idx}: expected key:value entry")

        key, raw_value = key_line.split(":", 1)
        key = key.strip()
        if not key:
            raise ValueError(f"{path}:{idx}: empty key")

        data[section][key] = parse_value(raw_value)

    return data


def escape_c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("Usage: generate_app_config_defaults.py <input_yaml> <output_header>", file=sys.stderr)
        return 2

    input_yaml = Path(argv[1])
    output_header = Path(argv[2])

    config = parse_simple_yaml(input_yaml)

    missing = [(s, k) for (s, k) in REQUIRED_KEYS if s not in config or k not in config[s]]
    if missing:
        missing_text = ", ".join(f"{s}.{k}" for (s, k) in missing)
        raise ValueError(f"Missing required keys: {missing_text}")

    enable_discovery = config["homeassistant"]["enable_discovery"]
    if not isinstance(enable_discovery, bool):
        raise ValueError("homeassistant.enable_discovery must be true or false")

    output = f"""#pragma once

/* Auto-generated from config/panel_config.yaml. Do not edit manually. */
#define APPCFG_DEFAULT_WIFI_SSID \"{escape_c_string(str(config['wifi']['ssid']))}\"
#define APPCFG_DEFAULT_WIFI_PASSWORD \"{escape_c_string(str(config['wifi']['password']))}\"
#define APPCFG_DEFAULT_MQTT_URI \"{escape_c_string(str(config['mqtt']['uri']))}\"
#define APPCFG_DEFAULT_MQTT_USERNAME \"{escape_c_string(str(config['mqtt']['username']))}\"
#define APPCFG_DEFAULT_MQTT_PASSWORD \"{escape_c_string(str(config['mqtt']['password']))}\"
#define APPCFG_DEFAULT_DISCOVERY_PREFIX \"{escape_c_string(str(config['homeassistant']['discovery_prefix']))}\"
#define APPCFG_DEFAULT_BASE_TOPIC \"{escape_c_string(str(config['homeassistant']['base_topic']))}\"
#define APPCFG_DEFAULT_ENABLE_DISCOVERY {1 if enable_discovery else 0}
"""

    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
