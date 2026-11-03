#!/usr/bin/env python3
"""Read W25Q128JV twice using flashrom; retain partial files and logs on failure."""
import argparse
import datetime
import shutil
import subprocess
from pathlib import Path
from verify_dumps import SIZE, verify

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--chip", default="W25Q128.V",
                        help="Exact flashrom chip name confirmed by probe; never force a mismatch")
    parser.add_argument("--speed", choices=("500k", "1M", "2M", "4M"), default="500k")
    parser.add_argument("--baseline", help="Verified 500 kHz read1.bin, required above 500k")
    parser.add_argument("--output", required=True, help="New directory; existing paths are refused")
    parser.add_argument("--safety-checked", action="store_true",
                        help="Confirm completion of docs/ELECTRICAL_SAFETY.zh-CN.md")
    args = parser.parse_args()
    if not args.safety_checked:
        parser.error("Complete electrical checklist and add --safety-checked; then physically arm BOOT")
    if not args.port.startswith("/dev/cu.usbmodem") or any(c in args.port for c in ",:"):
        parser.error("Use a native USB /dev/cu.usbmodem port")
    if args.speed != "500k" and not args.baseline:
        parser.error("Higher speed requires a verified 500 kHz baseline")
    baseline = Path(args.baseline).resolve() if args.baseline else None
    if baseline and (not baseline.is_file() or baseline.stat().st_size != SIZE):
        parser.error("Baseline must be exactly 16,777,216 bytes")
    executable = shutil.which("flashrom")
    if not executable:
        parser.error("Install flashrom: brew install flashrom")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    programmer = f"serprog:dev={args.port}:921600,spispeed={args.speed}"
    (output / "session.txt").write_text(
        f"UTC={datetime.datetime.now(datetime.timezone.utc).isoformat()}\n"
        f"programmer={programmer}\nchip={args.chip}\n"
        "electrical_checklist_acknowledged=yes\n", encoding="utf-8")
    for index in (1, 2):
        partial = output / f"read{index}.partial"
        command = [executable, "-p", programmer, "-c", args.chip, "-VV", "-r", str(partial)]
        print(f"Reading {index}/2 at {args.speed}; log: {output / f'read{index}.log'}", flush=True)
        with (output / f"read{index}.log").open("wb") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            raise RuntimeError(f"flashrom failed ({result.returncode}); inspect log; partial retained")
        if not partial.is_file() or partial.stat().st_size != SIZE:
            raise ValueError(f"Read {index} is not exactly {SIZE} bytes; partial retained")
        partial.rename(output / f"read{index}.bin")
    checked = verify(output / "read1.bin", output / "read2.bin", baseline)
    manifest = "".join(f"{digest}  {path.name}\n" for path, digest in checked[:2])
    (output / "SHA256SUMS").write_text(manifest, encoding="ascii")
    (output / "VERIFIED.txt").write_text(
        f"PASS: two complete {SIZE}-byte reads; byte-for-byte identical.\n"
        + ("Matches supplied 500 kHz baseline.\n" if baseline else "")
        + "Consistency does not prove correct wiring or authentic contents.\n", encoding="utf-8")
    print(manifest + "PASS: exact size and byte-for-byte comparison")

if __name__ == "__main__":
    main()
