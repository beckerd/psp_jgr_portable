"""Shared contact-scenery task clock and existing passage behavior."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BentenOverhangs(unittest.TestCase):
    def test_contact_cycle_and_existing_passages(self):
        with tempfile.TemporaryDirectory(prefix='jgr-overhang-') as folder:
            exe = Path(folder) / 'opening'
            subprocess.run(['cc', '-std=c99', '-O1', '-g',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            str(ROOT / 'psp/tests/opening_reactive.c'), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
