"""Formation navigation against production C, with synthetic inputs only."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GangNavigation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-gang-navigation-')
        cls.addClassCleanup(cls.build.cleanup)
        cls.exe = Path(cls.build.name) / 'gang-navigation'
        subprocess.run([
            'cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-local-typedef', '-Wno-unused-function', '-Wno-unused-parameter',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-DJGR_PORTABLE_ASSETS=1', '-DJGR_DIAGNOSTICS=0',
            '-Ipsp/tests/render_stubs', '-Ipsp/tests/stubs',
            'psp/tests/gang_navigation.c', '-lm', '-o', str(cls.exe),
        ], cwd=ROOT, check=True)

    def run_case(self, name):
        subprocess.run([str(self.exe), name], check=True, env={
            **os.environ, 'ASAN_OPTIONS': 'halt_on_error=1',
            'UBSAN_OPTIONS': 'halt_on_error=1',
        })

    def test_direct_pursuit_and_stopping(self):
        self.run_case('pursuit')

    def test_authored_portals_and_arrival(self):
        self.run_case('path')

    def test_player_leaves_and_reenters_navigation(self):
        self.run_case('graph')

    def test_leader_has_no_artificial_wall_turn(self):
        self.run_case('wall')


if __name__ == '__main__':
    unittest.main()
