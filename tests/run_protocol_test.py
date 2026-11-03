"""Compile production main.c with temporary IDF headers and SPI/GPIO spies."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
headers = [
    "freertos/FreeRTOS.h", "freertos/task.h", "driver/spi_master.h",
    "esp_vfs_dev.h", "sdkconfig.h", "esp_log.h", "driver/gpio.h",
    "driver/usb_serial_jtag.h", "driver/usb_serial_jtag_vfs.h",
]
with tempfile.TemporaryDirectory() as directory:
    include = Path(directory)
    for name in headers:
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "idf_stubs.h"\n')
    binary = include / "protocol-test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(include), "-I" + str(root / "tests"),
                    str(root / "tests/test_protocol.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
