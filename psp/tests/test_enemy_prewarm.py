"""Enemy bank prewarm must reuse first-use assets and preserve live effects."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EnemyPrewarmTests(unittest.TestCase):
    def test_bank_lifetime_attack_reuse_and_failure(self):
        with tempfile.TemporaryDirectory(prefix='jgr-enemy-prewarm-') as tmp:
            exe = Path(tmp) / 'prewarm'
            sources = ['psp/tests/enemy_prewarm_sanitize.c'] + [
                'psp/src/' + name + '.c' for name in
                ('enemyshot', 'enemy_missile', 'enemy_shell', 'enemy_wave', 'enemy_gas')]
            # Unused parts of the real world adapter reference unrelated
            # renderer/player code. Keep the production residency and shot
            # paths, not a copied implementation or an asset fixture.
            dead_code = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
            subprocess.run(['cc', '-O1', '-g', '-ffp-contract=off',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-ffunction-sections', '-fdata-sections', dead_code,
                            '-DJGR_PORTABLE_ASSETS=1',
                            '-Ipsp/tests/stubs', *sources, '-lm', '-o', str(exe)],
                           cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe)], cwd=ROOT, check=True,
                                    capture_output=True, text=True, timeout=30)
            self.assertIn('checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
