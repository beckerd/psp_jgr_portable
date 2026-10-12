from pathlib import Path
import unittest
from converter_sources import select, references, exported_lua


class ConverterSelection(unittest.TestCase):
    def test_closure_keeps_converter_inputs_but_excludes_private_oracles(self):
        names=set(select(Path(__file__).resolve().parents[2]))
        for name in ('jgs_convert.py','jgs_cans.py','ninjalib.py','pvr_decode.py',
                     'staff_route_assets.py','flycast_recruit_demo_audio.lua','ppsspp_gpu_capture.mjs'):
            self.assertIn(name,names)
        for name in ('flycast_spray_score_oracle.lua','flycast_garage_selection_cast.lua'):
            self.assertNotIn(name,names)

    def test_import_and_subprocess_dependencies(self):
        self.assertEqual(references('import first\nfrom second import f\nrun("tools/third.py")'),
                         {'first.py','second.py','third.py'})
        self.assertEqual(references('run("helper.mjs")'), {'helper.mjs'})

    def test_public_capture_has_no_developer_root(self):
        text="local root='/Users/example/Project/JGR/decompile/'\ndofile(root..'tools/task.lua')\n"
        result=exported_lua(text)
        self.assertNotIn('/Users/',result)
        self.assertIn("os.getenv('JGR_REFERENCE_ROOT')",result)


if __name__=='__main__':unittest.main()
