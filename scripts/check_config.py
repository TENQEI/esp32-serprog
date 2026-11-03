#!/usr/bin/env python3
"""Fail closed on an unsafe or stale sdkconfig."""
import re
import sys
from pathlib import Path

def check(path):
    text = Path(path).read_text()
    required = [
        "CONFIG_IDF_TARGET_ESP32S3=y",
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
        "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        "CONFIG_ESPTOOLPY_FLASHMODE_DIO=y",
        "CONFIG_LOG_DEFAULT_LEVEL_NONE=y",
        "CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y",
    ]
    for line in required:
        if line not in text.splitlines():
            raise ValueError("Missing required setting: " + line)
    for key in ("CONFIG_SPIRAM", "CONFIG_ESP32_SERPROG_WIFI_ENABLED"):
        if key + "=y" in text.splitlines():
            raise ValueError("Unsafe setting: " + key)
    match = re.search(r"^CONFIG_READER_MAX_SPI_HZ=(\d+)$", text, re.M)
    if not match or int(match[1]) not in (500000, 1000000, 2000000, 4000000):
        raise ValueError("SPI ceiling must be an approved step")
    print("Configuration OK; SPI ceiling:", match[1], "Hz")

if __name__ == "__main__":
    check(sys.argv[1])
