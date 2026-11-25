#!/usr/bin/env python3
"""Build an ESP Web Tools manifest from ESP-IDF's authoritative flash map."""

from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware" / "csi_receiver" / "build"
WEB_FIRMWARE = ROOT / "web" / "firmware"
FLASH_MAP = BUILD / "flasher_args.json"


def main() -> None:
    if not FLASH_MAP.is_file():
        raise SystemExit(f"ESP-IDF flash map not found: {FLASH_MAP}")

    data = json.loads(FLASH_MAP.read_text(encoding="utf-8"))
    flash_files = data.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise SystemExit("flasher_args.json does not contain a valid flash_files map")

    WEB_FIRMWARE.mkdir(parents=True, exist_ok=True)
    for old_file in WEB_FIRMWARE.glob("*.bin"):
        old_file.unlink()
    manifest_path = WEB_FIRMWARE / "manifest.json"
    if manifest_path.exists():
        manifest_path.unlink()

    parts = []
    for raw_offset, raw_path in flash_files.items():
        source = Path(raw_path)
        if not source.is_absolute():
            source = BUILD / source
        source = source.resolve()
        if not source.is_file():
            raise SystemExit(f"Firmware image listed by ESP-IDF is missing: {source}")

        offset = int(str(raw_offset), 0)
        filename = f"part_{offset:06x}_{source.name}"
        shutil.copy2(source, WEB_FIRMWARE / filename)
        parts.append({"path": f"./{filename}", "offset": offset})

    parts.sort(key=lambda part: part["offset"])
    manifest = {
        "name": "ESP32-S3 WiFi CSI Sensing",
        "version": os.environ.get("FIRMWARE_VERSION", "0.1.0"),
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": parts,
            }
        ],
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
