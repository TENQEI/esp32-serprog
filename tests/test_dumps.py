import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_dumps import SIZE, verify
from check_config import check

class DumpTests(unittest.TestCase):
    def test_full_size_comparison_and_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b, baseline = [Path(directory) / n for n in ("a", "b", "baseline")]
            content = bytes(range(256)) * (SIZE // 256)
            for path in (a, b, baseline):
                path.write_bytes(content)
            self.assertEqual(verify(a, b, baseline)[0][1], verify(a, b)[1][1])
            with b.open("r+b") as stream:
                stream.seek(SIZE - 1)
                stream.write(b"X")
            with self.assertRaises(ValueError):
                verify(a, b)
            b.write_bytes(content[:-1])
            with self.assertRaises(ValueError):
                verify(a, b)
            b.write_bytes(content)
            baseline.write_bytes(b"X" * SIZE)
            with self.assertRaises(ValueError):
                verify(a, b, baseline)
    def test_flat_invalid_and_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / "a", Path(directory) / "b"
            for value in (0, 255):
                a.write_bytes(bytes([value]) * SIZE)
                b.write_bytes(bytes([value]) * SIZE)
                with self.assertRaises(ValueError):
                    verify(a, b)
            with self.assertRaises(FileNotFoundError):
                verify(a, Path(directory) / "missing")

if __name__ == "__main__":
    unittest.main()
