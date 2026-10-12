"""Police direction arrow endpoint rounding must not enter sqrt's error path."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PoliceArrowTests(unittest.TestCase):
    def test_finite_angles_and_full_arrow_updates(self):
        with tempfile.TemporaryDirectory(prefix='jgr-police-arrow-') as folder:
            exe = Path(folder) / 'arrow'
            subprocess.run(['cc', '-std=c99', '-O2', '-g', '-ffp-contract=off',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            str(ROOT / 'psp/tests/police_arrow_math.c'), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
