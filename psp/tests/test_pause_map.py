"""Pause-map progression against production C, with no game asset dependency."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PauseMapProgress(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-pause-map-')
        cls.addClassCleanup(cls.build.cleanup)
        cls.exe = Path(cls.build.name) / 'pause-map'
        subprocess.run([
            'cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-local-typedef', '-Wno-unused-function', '-Wno-unused-parameter',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-DJGR_PORTABLE_ASSETS=1', '-DJGR_DIAGNOSTICS=0',
            '-Ipsp/tests/render_stubs', '-Ipsp/tests/stubs',
            'psp/tests/pause_map_progress.c', '-lm', '-o', str(cls.exe),
        ], cwd=ROOT, check=True)

    def run_case(self, name):
        subprocess.run([str(self.exe), name], check=True, env={
            **os.environ, 'ASAN_OPTIONS': 'halt_on_error=1',
            'UBSAN_OPTIONS': 'halt_on_error=1',
        })

    def test_explosion_section_completion_and_reentry(self):
        self.run_case('explosion')

    def test_fight_or_flight_section_completion_and_reentry(self):
        self.run_case('fight')

    def test_benten_burning_sections_and_empty_connector(self):
        self.run_case('burning')

    def test_objective_filtering_and_other_missions(self):
        self.run_case('scope')


if __name__ == '__main__':
    unittest.main()
