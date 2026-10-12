"""Real-thread lifecycle tests using synthetic ADX, without game assets."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MessageAsyncTests(unittest.TestCase):
    def test_reader_lifecycle_cpu_and_media_engine(self):
        with tempfile.TemporaryDirectory(prefix='jgr-radio-async-') as tmp:
            for me in (False, True):
                with self.subTest(media_engine=me):
                    exe = Path(tmp)/('me' if me else 'cpu')
                    cmd = ['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                           '-ffp-contract=off', '-fsanitize=address,undefined',
                           '-fno-sanitize-recover=all', '-pthread', '-DJGR_DIAGNOSTICS=0',
                           '-DJGR_MESSAGE_ASYNC_TEST', '-Ipsp/tests/audio_stubs', '-Ipsp/tests/stubs']
                    if me:
                        cmd += ['-DJGR_AUDIO_ME_TEST']
                    cmd += ['psp/tests/message_async_sanitize.c']
                    cmd += ['psp/src/'+name+'.c' for name in ('osb', 'smsb', 'adx', 'movie_voice', 'audio_job')]
                    cmd += ['-lm', '-o', str(exe)]
                    build = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
                    run = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True, timeout=40)
                    self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
                    self.assertIn('shutdown passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
