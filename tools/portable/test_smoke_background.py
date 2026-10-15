from pathlib import Path
import tempfile
import unittest

import smoke


class SmokeBackgroundTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'EBOOT.PBP').write_bytes(b'synthetic bundled executable')
        assets = self.source / 'data/assets'
        assets.mkdir(parents=True)
        (assets / 'test.bin').write_bytes(b'synthetic asset')
        (assets / 'assets-manifest.txt').write_text('JGR_ASSETS 1\nlayout 2\ncount 1\n15 00000000 test.bin\n')

    def test_background_is_optional(self):
        destination = self.root / 'no-background'
        smoke.clone(self.source, destination, 'missing', None)
        self.assertFalse((destination / 'data/setup').exists())
        self.assertEqual([p.name for p in destination.iterdir() if p.is_file()], ['EBOOT.PBP'])

    def test_recovery_requires_completed_installer_error(self):
        self.assertIsNone(smoke.setup_failure_status('Preparing game data: 4 / 1892'))
        self.assertEqual(smoke.setup_failure_status('SETUP status=-1 file=zones6.bin detail=Cannot reserve space\n'), -1)
        self.assertEqual(smoke.setup_failure_status('SETUP status=-3 file=fontmain.bin detail=Add the original disc\n'), -3)

    def test_each_case_gets_an_independent_background_copy(self):
        background = self.source / 'data/setup/background.jbg'
        background.parent.mkdir()
        background.write_bytes(b'synthetic setup background')
        for case in ('title', 'missing', 'corrupt'):
            with self.subTest(case=case):
                destination = self.root / case
                smoke.clone(self.source, destination, case, None)
                copy = destination / 'data/setup/background.jbg'
                self.assertFalse(copy.is_symlink())
                self.assertEqual(copy.read_bytes(), background.read_bytes())
                copy.write_bytes(b'changed isolated background')
                self.assertEqual(background.read_bytes(), b'synthetic setup background')
                if case == 'missing':
                    self.assertFalse((destination / 'data/assets').exists())


if __name__ == '__main__':
    unittest.main()
