import hashlib
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import export_inputs


class ExportInputs(unittest.TestCase):
    def test_staging_does_not_reuse_locally_generated_garage_specs(self):
        import pipeline
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'source'
            tree = Path(directory) / 'conversion'
            (root/'decompile/tools').mkdir(parents=True)
            (root/'psp/src').mkdir(parents=True)
            for name in pipeline.SPECS:
                (root/'decompile'/name).write_text('Source-address recipe\n')
            for name in ('garage_spec.txt', 'garage_pending_spec.txt'):
                (root/'decompile'/name).write_text('Synthetic:texmap=6,4\n')
            for name in ('stage_texture_usage.inc', 'stage_depth_data.inc'):
                (root/'psp/src'/name).write_text('/* synthetic port metadata */\n')
            pipeline.seed_sources(root, tree)
            for name in ('garage_spec.txt', 'garage_pending_spec.txt'):
                self.assertFalse((tree/'decompile'/name).exists())
            nodes = {node.name: node for node in pipeline.graph(tree)}
            for name in ('garage', 'garage_pending'):
                self.assertEqual([cmd[-1] for cmd in nodes[name].commands], ['--write', '--bake'])

    def test_native_harnesses_need_explicit_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('psp/tools/meprx', 'psp/tools/crashprx', 'tools/portable/replay'):
                (root/name).mkdir(parents=True)
            (root/'tools/portable/replay/unreviewed.c').write_text('int new_probe;\n')
            (root/'tools/portable/replay/verify_staff_reuse.py').write_text('# obsolete private experiment\n')
            with patch('converter_sources.select', return_value=[]), patch('pipeline.SPECS', []):
                names = export_inputs.names(root)
            self.assertIn('tools/portable/replay/session_probe.c', names)
            self.assertNotIn('tools/portable/replay/unreviewed.c', names)
            self.assertIn('tools/portable/replay/test_staff_reuse.c', names)
            self.assertIn('tools/portable/replay/staff_reuse_api_probe.c', names)
            self.assertNotIn('tools/portable/replay/verify_staff_reuse.py', names)

    def test_input_schema_is_not_just_truthiness(self):
        good = dict(schema=1, files=[dict(path='tool.py', sha256='1'*64, review='Reviewed extractor')])
        for value in (True, 1.0, '1'):
            policy = dict(good, schema=value)
            with self.assertRaises(ValueError): export_inputs.validate(policy)
        for key, value in (('review', True), ('review', ' '), ('sha256', None), ('sha256', 'xyz'), ('path', '../tool.py')):
            policy = copy.deepcopy(good)
            policy['files'][0][key] = value
            with self.assertRaises(ValueError): export_inputs.validate(policy)

    def test_snapshot_keeps_verified_bytes_after_source_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/'tool.py'; path.write_bytes(b'print("reviewed")\n')
            policy = dict(schema=1, files=[dict(path='tool.py', sha256=hashlib.sha256(path.read_bytes()).hexdigest(), review='Synthetic input')])
            with patch.object(export_inputs, 'names', return_value=['tool.py']):
                inputs = export_inputs.snapshot(root, policy)
                path.write_bytes(b'print("changed")\n')
                self.assertEqual(inputs['tool.py'], b'print("reviewed")\n')

    def test_changed_new_missing_and_linked_tools_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'tool.py'
            path.write_text('print("synthetic")\n')
            policy = dict(schema=1, files=[dict(path='tool.py',
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(), review='Synthetic input')])
            with patch.object(export_inputs, 'names', return_value=['tool.py']):
                self.assertEqual(export_inputs.verify(root, policy), 1)
                path.write_text('print("changed")\n')
                with self.assertRaisesRegex(ValueError, 'changed'):
                    export_inputs.verify(root, policy)
            with patch.object(export_inputs, 'names', return_value=['tool.py', 'new.py']):
                with self.assertRaisesRegex(ValueError, 'Unreviewed'):
                    export_inputs.verify(root, policy)
            with patch.object(export_inputs, 'names', return_value=[]):
                with self.assertRaisesRegex(ValueError, 'no longer selected'):
                    export_inputs.verify(root, policy)
            path.unlink()
            (root / 'outside.py').write_text('x = 1\n')
            path.symlink_to(root / 'outside.py')
            with patch.object(export_inputs, 'names', return_value=['tool.py']):
                with self.assertRaisesRegex(ValueError, 'Symlink'):
                    export_inputs.verify(root, policy)


if __name__ == '__main__':
    unittest.main()
