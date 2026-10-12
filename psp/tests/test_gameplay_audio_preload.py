"""Background gameplay bank loading keeps the caller free and joins safely."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GameplayAudioPreloadTests(unittest.TestCase):
    def test_worker_lifetime_and_failures(self):
        with tempfile.TemporaryDirectory(prefix='jgr-gameplay-audio-') as tmp:
            # Only transport declarations are stubbed: both worker branches
            # use real pthreads, including the PSP priority/start-failure test.
            (Path(tmp) / 'pspkernel.h').write_text('''
#ifndef TEST_PSP_KERNEL_H
#define TEST_PSP_KERNEL_H
typedef int SceUID;
typedef unsigned SceSize;
int sceKernelCreateThread(const char *,int (*)(SceSize,void *),int,unsigned,unsigned,void *);
int sceKernelStartThread(int,unsigned,void *);
int sceKernelWaitThreadEnd(int,unsigned *);
int sceKernelDeleteThread(int);
#endif
''')
            for psp in (False, True):
                with self.subTest(psp_api=psp):
                    exe = Path(tmp) / ('psp' if psp else 'host')
                    flags = ['-D__PSP__', '-I' + tmp] if psp else []
                    result = subprocess.run([
                        'cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        *flags, '-Ipsp/src', 'psp/tests/gameplay_audio_preload_sanitize.c',
                        '-o', str(exe)], cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(exe)], cwd=tmp, capture_output=True,
                                            text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('retirement and reuse passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
