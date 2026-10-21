#!/usr/bin/env python3
"""Copy compiled ESP32 firmware and boot components into web-installer/.
Run *after* `pio run` on a machine with PlatformIO installed.
"""
from pathlib import Path
import os
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "web-installer"
BUILT = ROOT / ".pio" / "build" / "esp32dev"
BASE = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
ARTIFACTS = {
    "firmware.bin": BUILT / "firmware.bin",
    "bootloader.bin": BUILT / "bootloader.bin",
    "partitions.bin": BUILT / "partitions.bin",
}


def main():
    missing = [str(path) for path in ARTIFACTS.values() if not path.is_file()]
    if missing:
        print("Missing build outputs. Run `pio run` in the project folder first:")
        print("\n".join(missing))
        return 1
    candidates = [
        BUILT / "boot_app0.bin",
        BASE / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
        BASE / "packages" / "framework-arduinoespressif32" / "tools" / "boot_app0.bin",
    ]
    boot_app = next((p for p in candidates if p.is_file()), None)
    if boot_app is None:
        print("Cannot locate boot_app0.bin in the PlatformIO ESP32 framework.")
        return 1
    ARTIFACTS["boot_app0.bin"] = boot_app
    OUT.mkdir(exist_ok=True)
    for name, path in ARTIFACTS.items():
        shutil.copyfile(path, OUT / name)
        print("Copied", name, (OUT / name).stat().st_size, "bytes")
    print("Installer assets prepared in", OUT)
    print("Serve on http://localhost:8000 using `python -m http.server 8000 --directory web-installer`")
    print("Open http://localhost:8000/ in Chrome or Edge with your ESP32 connected by USB.")
    print("To share an installer on GitHub Pages, publish web-installer/ over HTTPS.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
