"""Load-time enemy preparation follows this mission's future event references."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MissionEnemyPreloadTests(unittest.TestCase):
    def test_event_coverage_member_filters_and_bounds(self):
        with tempfile.TemporaryDirectory(prefix='jgr-mission-enemy-preload-') as tmp:
            exe = Path(tmp) / 'preload'
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-Ipsp/tests/stubs', 'psp/tests/mission_enemy_preload_sanitize.c',
                            '-o', str(exe)], cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe)], cwd=ROOT, check=True,
                                    capture_output=True, text=True, timeout=30)
            self.assertIn('coverage/bounds checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
