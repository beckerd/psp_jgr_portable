"""Check the title resolve's pixels and EDRAM ownership under sanitizers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TitleSmoothingTests(unittest.TestCase):
    def test_tiled_resolve(self):
        with tempfile.TemporaryDirectory(prefix='jgr-title-smoothing-') as tmp:
            executable = Path(tmp) / 'test'
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-DJGR_TEST_GFX=1',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            'psp/tests/title_smoothing.c', '-lm', '-o', str(executable)],
                           cwd=ROOT, check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
