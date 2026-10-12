"""Threaded route reader preserves samples and avoids steady-play disk reads."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TitleRoutePrefetchTests(unittest.TestCase):
    def test_threaded_samples_lifetimes_failures_and_memory_bound(self):
        with tempfile.TemporaryDirectory(prefix='jgr-route-prefetch-', dir='/tmp') as tmp:
            tmp = Path(tmp)
            reference = tmp / 'reference.c'
            reference.write_text('\n'.join([
                '#define title_route_load sync_title_route_load',
                '#define title_route_free sync_title_route_free',
                '#define title_route_resume sync_title_route_resume',
                '#define title_route_sample sync_title_route_sample',
                '#include "title_route.c"', '']))
            exe = tmp / 'prefetch'
            result = subprocess.run([
                'cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
                '-ffp-contract=off', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-Ipsp/src',
                'psp/tests/title_route_prefetch_sanitize.c', str(reference),
                '-lm', '-o', str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe), str(tmp)], cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('no main-thread sequential reads', result.stdout)


if __name__ == '__main__':
    unittest.main()
