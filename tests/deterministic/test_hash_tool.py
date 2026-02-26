import subprocess
import tempfile
import unittest
from pathlib import Path


class HashToolTests(unittest.TestCase):
    def test_compare_binary_hash(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            p1 = Path(td) / "a.bin"
            p2 = Path(td) / "b.bin"
            p1.write_bytes(b"thagc")
            p2.write_bytes(b"thagc")
            subprocess.check_call(
                [
                    "python3",
                    "tools/compare/compare_binary_hash.py",
                    "--left",
                    str(p1),
                    "--right",
                    str(p2),
                ]
            )


if __name__ == "__main__":
    unittest.main()
