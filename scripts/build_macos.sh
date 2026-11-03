#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../src/serprog"
command -v idf.py >/dev/null || { echo "先 source ESP-IDF v6.0/export.sh" >&2; exit 1; }
idf.py --version | grep -Eq 'ESP-IDF v6\.0([. -]|$)' || { echo "需要 ESP-IDF v6.0.x" >&2; exit 1; }
# Use a separate sdkconfig so upstream/local configuration cannot silently enable Wi-Fi.
idf.py -B build-reader -D SDKCONFIG=sdkconfig.reader set-target esp32s3
idf.py -B build-reader -D SDKCONFIG=sdkconfig.reader build
python3 ../../scripts/check_config.py sdkconfig.reader
