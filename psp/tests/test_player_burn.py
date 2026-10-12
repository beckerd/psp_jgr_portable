"""Player fire uses clipped source-material draws without advancing simulation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PlayerBurnTests(unittest.TestCase):
    def test_render_without_enemy_streams_and_pause_stability(self):
        with tempfile.TemporaryDirectory(prefix='jgr-player-burn-') as tmp:
            executable = Path(tmp) / 'render'
            dead_code = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
            result = subprocess.run([
                'cc', '-O1', '-g', '-ffp-contract=off', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-ffunction-sections', '-fdata-sections', dead_code,
                '-DJGR_PORTABLE_ASSETS=1', '-Ipsp/tests/stubs', '-Ipsp/tests/render_stubs',
                'psp/tests/player_burn_render.c', '-lm', '-o', str(executable)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
