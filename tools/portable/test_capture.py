from pathlib import Path
import tempfile
import unittest

import capture


class CaptureSetupTests(unittest.TestCase):
    def test_fresh_boot_staging_removes_state_dependency(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            entry = capture.stage_scripts(capture.ROOT, out, 'staff')
            for name in ('flycast_stage_reference.lua', 'flycast_ending_reference.lua'):
                text = (out / 'scripts/tools' / name).read_text()
                self.assertNotIn('flycast.emulator.loadState(1)', text)
                self.assertIn('portable_entered', text)
                self.assertNotIn('/Users/', text)
            staff = (out / 'scripts/tools/flycast_staff_route_reference.lua').read_text()
            self.assertNotIn('/Users/', staff)
            self.assertIn('flycast.emulator.loadState=forbidden', entry.read_text())
            self.assertIn("os.rename(path..'.tmp',path)", entry.read_text())
            self.assertIn('installed and count>0',staff)
            self.assertIn('m.read16(0x8c01dc40)==0x2fe6',staff)
            self.assertIn('xpcall(callback,function(problem)',entry.read_text())
            self.assertIn('error(capture_stop)',entry.read_text())
            self.assertIn('if not ok and problem~=capture_stop then',entry.read_text())

    def test_title_uses_complete_callback_hook(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            capture.stage_scripts(capture.ROOT, out, 'title')
            self.assertEqual((out / 'scripts/tools/flycast_title_reference.lua').read_bytes(),
                             (capture.ROOT / 'tools/portable/title_capture.lua').read_bytes())


if __name__ == '__main__':
    unittest.main()
