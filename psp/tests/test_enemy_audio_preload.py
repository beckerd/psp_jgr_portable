"""Relevant enemy attacks stay resident without changing live transports."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EnemyAudioPreloadTests(unittest.TestCase):
    def test_real_bank_cpu_and_media_engine(self):
        with tempfile.TemporaryDirectory(prefix='jgr-enemy-audio-preload-') as tmp:
            for me in (False, True):
                with self.subTest(media_engine=me):
                    exe = Path(tmp) / ('me' if me else 'cpu')
                    cmd = ['cc', '-O1', '-g', '-ffp-contract=off',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                           '-DJGR_DIAGNOSTICS=0', '-Ipsp/tests/audio_stubs', '-Ipsp/tests/stubs']
                    if me:
                        cmd += ['-DJGR_AUDIO_ME_TEST']
                    cmd += ['psp/tests/enemy_audio_preload_sanitize.c']
                    cmd += ['psp/src/' + name + '.c' for name in
                            ('osb', 'smsb', 'adx', 'movie_voice', 'audio_job')]
                    cmd += ['-lm', '-o', str(exe)]
                    build = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(exe)], cwd=ROOT/'psp', capture_output=True,
                                         text=True, timeout=45)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn('retirement passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
