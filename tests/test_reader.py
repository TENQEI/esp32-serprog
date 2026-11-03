from pathlib import Path
import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import read_twice
from verify_dumps import SIZE

DATA = bytes(range(256)) * (SIZE // 256)

class ReaderTests(unittest.TestCase):
    def invoke(self, output, fake_run, extra=()):
        argv = ["read_twice.py", "--port", "/dev/cu.usbmodemTEST", "--output", str(output),
                "--safety-checked", *extra]
        with patch.object(sys, "argv", argv), patch("read_twice.shutil.which", return_value="flashrom"), \
             patch("read_twice.subprocess.run", side_effect=fake_run), contextlib.redirect_stdout(io.StringIO()):
            read_twice.main()

    @staticmethod
    def good_read(command, **kwargs):
        assert "-r" in command and "-w" not in command and "-f" not in command
        Path(command[-1]).write_bytes(DATA)
        return subprocess.CompletedProcess(command, 0)

    def test_two_reads_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "out"
            self.invoke(out, self.good_read)
            self.assertTrue((out / "VERIFIED.txt").is_file())
            self.assertEqual((out / "read1.bin").stat().st_size, SIZE)
            self.assertEqual(len((out / "SHA256SUMS").read_text().splitlines()), 2)
            with self.assertRaises(FileExistsError):
                self.invoke(out, self.good_read)

    def test_failure_partial_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "out"
            def failed(command, **kwargs):
                Path(command[-1]).write_bytes(b"partial")
                return subprocess.CompletedProcess(command, 1)
            with self.assertRaises(RuntimeError):
                self.invoke(out, failed)
            self.assertTrue((out / "read1.partial").exists())
            self.assertFalse((out / "VERIFIED.txt").exists())
            self.assertFalse((out / "SHA256SUMS").exists())

    def test_success_exit_but_truncated_or_different(self):
        with tempfile.TemporaryDirectory() as directory:
            for kind in ("truncated", "different"):
                calls = 0
                def bad(command, **kwargs):
                    nonlocal calls
                    calls += 1
                    data = DATA if calls == 1 else (DATA[:-1] if kind == "truncated" else b"X" * SIZE)
                    Path(command[-1]).write_bytes(data)
                    return subprocess.CompletedProcess(command, 0)
                out = Path(directory) / kind
                with self.assertRaises(ValueError):
                    self.invoke(out, bad)
                self.assertFalse((out / "VERIFIED.txt").exists())

    def test_high_speed_requires_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.invoke(Path(directory) / "out", self.good_read, ("--speed", "1M"))
            self.assertFalse((Path(directory) / "out").exists())

if __name__ == "__main__":
    unittest.main()
