#!/usr/bin/env python3
"""Exact-size, byte comparison and SHA-256 verification; no hardware I/O."""
import argparse
import hashlib
from pathlib import Path

SIZE = 16_777_216

def verify(first, second, baseline=None):
    paths = [Path(first), Path(second)]
    if baseline is not None:
        paths.append(Path(baseline))
    for path in paths:
        if path.stat().st_size != SIZE:
            raise ValueError(f"{path}: expected {SIZE} bytes, got {path.stat().st_size}")
    hashes = [hashlib.sha256() for _ in paths]
    flat = [True, True]
    with paths[0].open("rb") as a, paths[1].open("rb") as b:
        base = paths[2].open("rb") if baseline is not None else None
        try:
            while True:
                left, right = a.read(65536), b.read(65536)
                if not left:
                    break
                if left != right:
                    raise ValueError("Two dumps differ byte-for-byte")
                if base:
                    original = base.read(len(left))
                    if left != original:
                        raise ValueError("Higher-speed dump differs from 500 kHz baseline")
                    hashes[2].update(original)
                hashes[0].update(left)
                hashes[1].update(right)
                flat[0] &= left == b"\x00" * len(left)
                flat[1] &= left == b"\xff" * len(left)
        finally:
            if base:
                base.close()
    if any(flat):
        raise ValueError("Dump is entirely 00 or FF; investigate electrical/contact problems")
    return [(p, h.hexdigest()) for p, h in zip(paths, hashes)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first")
    parser.add_argument("second")
    parser.add_argument("--baseline")
    args = parser.parse_args()
    for path, digest in verify(args.first, args.second, args.baseline):
        print(f"{digest}  {path}")

if __name__ == "__main__":
    main()
