#!/usr/bin/env bash
set -euo pipefail
[[ $# == 1 ]] || { echo "Usage: bash scripts/flash_macos.sh /dev/cu.usbmodemXXXX" >&2; exit 1; }
[[ "$1" == /dev/cu.usbmodem* ]] || { echo "使用原生 USB 的 cu.usbmodem 端口" >&2; exit 1; }
cd "$(dirname "$0")/../src/serprog"
[[ -f build-reader/flasher_args.json ]] || { echo "先运行 build_macos.sh" >&2; exit 1; }
python3 ../../scripts/check_config.py sdkconfig.reader
echo "必须断开测试夹及目标板所有连线；这里只烧录 ESP32 开发板。"
read -r -p "输入 FLASH-ESP32-ONLY 继续: " confirmation
[[ "$confirmation" == FLASH-ESP32-ONLY ]] || exit 1
idf.py -B build-reader -D SDKCONFIG=sdkconfig.reader -p "$1" flash
