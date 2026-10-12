"""A stale or unbound build receipt must never clear the export data gate."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from audit_build import source_binding
from export import verified_build_audit
import export as exporter
import export_inputs
import public_review
import source_data


class BuildAuditBinding(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.build=self.root/'psp'
        for folder in ('src','tools/meprx','tools/crashprx'):
            (self.build/folder).mkdir(parents=True)
        (self.root/'decompile/tools').mkdir(parents=True)
        (self.root/'decompile/tools/crash_build_id.py').write_text('# source fingerprint generator\n')
        (self.build/'Makefile').write_text('CFLAGS=-O2\n')
        (self.build/'src/engine.c').write_text('int table[10];\n')
        self.report=dict(schema=1,passed=True,elf_sha256='1'*64,eboot_sha256='2'*64,
            source_binding=source_binding(self.build),asset_identity_sha256='3'*64,external_asset_symbols=10,
            external_asset_bytes=40,compiler_dependency_files=1,active_original_data_includes=[])
        self.path=self.root/'audit.json'

    def tearDown(self):
        self.temp.cleanup()

    def verify(self):
        self.path.write_text(json.dumps(self.report))
        return verified_build_audit(self.path,self.build)

    def test_matching_compiled_audit_retains_artifact_and_source_identity(self):
        result=self.verify()
        self.assertEqual(result['source_binding'],self.report['source_binding'])
        self.assertEqual(result['eboot_sha256'],'2'*64)
        self.assertEqual(len(result['audit_sha256']),64)
        self.assertNotIn(str(self.root),json.dumps(result))

    def test_stale_or_unbound_audit_fails(self):
        (self.build/'src/engine.c').write_text('int table[11];\n')
        with self.assertRaisesRegex(ValueError,'does not match'):
            self.verify()
        self.report.pop('source_binding')
        with self.assertRaisesRegex(ValueError,'does not match'):
            self.verify()

    def test_failed_incomplete_or_active_original_data_audit_fails(self):
        cases=[('passed',False),('schema',2),('schema',True),('active_original_data_includes',['original.inc']),
               ('external_asset_symbols',True),('external_asset_bytes',0),
               ('compiler_dependency_files',0),('eboot_sha256','not a hash')]
        for field,value in cases:
            with self.subTest(field=field):
                original=self.report[field];self.report[field]=value
                with self.assertRaises(ValueError):self.verify()
                self.report[field]=original

    def test_split_source_requires_complete_split_artifact_evidence(self):
        (self.build/'Makefile').write_text('python3 ../tools/portable/build_device_setup.py\n')
        tools=self.root/'tools/portable';tools.mkdir(parents=True)
        for name in ('build_device_setup.py','bundle.py'):(tools/name).write_text('# authored fixture\n')
        self.report['source_binding']=source_binding(self.build)
        with self.assertRaisesRegex(ValueError,'split executable'):self.verify()
        hashes=('setup_elf_sha256','setup_prx_sha256','setup_makefile_sha256','game_prx_sha256',
                'me_elf_sha256','crash_elf_sha256','bundle_sha256','recipe_sha256','notice_sha256','receipt_sha256')
        split=dict.fromkeys(hashes,'a'*64)
        split.update(schema=1,setup_dependency_files=1,game_dependency_files=1,setup_source_files=1,
            bundle_roles=[1,2,3,4,5],game_importer_symbols=[],setup_game_asset_symbols=[])
        self.report['split_executable']=split;self.report['compiler_dependency_files']=2
        self.assertEqual(self.verify()['split_executable'],split)
        self.report['compiler_dependency_files']=1
        with self.assertRaisesRegex(ValueError,'dependency count'):self.verify()
        self.report['compiler_dependency_files']=2
        (tools/'build_device_setup.py').write_text('# changed build instructions\n')
        with self.assertRaisesRegex(ValueError,'does not match'):self.verify()


class SourceExportGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name).resolve()
        self.root = self.base/'different-checkout'
        self.root.mkdir()
        self.write('psp/src/game.c', b'int synthetic_game;\n')
        source = self.root/'psp/src/game.c'
        manifest = dict(schema=source_data.SCHEMA, files=[dict(path='psp/src/game.c',
            kind='portable_implementation', sha256=hashlib.sha256(source.read_bytes()).hexdigest(), review='Synthetic native implementation')])
        self.write('tools/portable/source_data_manifest.json', json.dumps(manifest).encode())
        self.policy = dict(schema=1, whole_tree_review_complete=False, findings=[])
        self.write(public_review.POLICY_PATH, json.dumps(self.policy).encode())
        self.write('psp/Makefile.portable', b'all:\n\ttrue\n')
        self.write('decompile/tools/capture.lua', b'local root="/Users/example/Project/JGR/decompile/"\nlocal reviewed=true\n')
        self.selected = ['tools/portable/source_data_manifest.json', public_review.POLICY_PATH,
                         'psp/Makefile.portable', 'decompile/tools/capture.lua']
        self.refresh_inputs()
        self.names_patch = patch.object(export_inputs, 'names', return_value=self.selected)
        self.names_patch.start()

    def tearDown(self):
        self.names_patch.stop()
        self.temp.cleanup()

    def write(self, name, data):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def refresh_inputs(self):
        rows = [dict(path=name, sha256=hashlib.sha256((self.root/name).read_bytes()).hexdigest(), review='Synthetic reviewed input') for name in self.selected]
        self.write(export_inputs.MANIFEST, json.dumps(dict(schema=1, files=rows)).encode())

    def approve_candidate(self):
        candidate = self.base/'private'
        exporter.export(self.root, candidate, require_public_clean=False)
        self.policy.update(whole_tree_review_complete=True, reviewed_files=public_review.inventory(candidate))
        self.write(public_review.POLICY_PATH, json.dumps(self.policy).encode())
        self.refresh_inputs()

    def test_default_gate_private_candidate_and_cross_checkout_policies(self):
        with self.assertRaisesRegex(ValueError, 'Public export refused'):
            exporter.export(self.root, self.base/'blocked')
        self.assertFalse((self.base/'blocked').exists())
        self.approve_candidate()
        result = exporter.export(self.root, self.base/'approved')
        self.assertTrue(result['public_content_review_passed'])
        self.assertNotIn('public source/content review', result['remaining_gates'])
        self.assertIn('PSP hardware validation', result['remaining_gates'])
        self.assertTrue(public_review.review(self.base/'approved')['passed'])
        # Receipt data is outside the self-referential hash but cannot carry
        # arbitrary extra text or a stale file inventory.
        path = self.base/'approved/EXPORT-REVIEW.json'
        receipt = json.loads(path.read_text()); receipt['extra_payload'] = [1, 2, 3]
        path.write_text(json.dumps(receipt))
        self.assertFalse(public_review.review(self.base/'approved')['passed'])

    def test_copies_and_lua_rewrites_use_verified_snapshot(self):
        self.approve_candidate()
        original_stage = source_data.stage
        def changed_after_snapshot(root, tree, manifest):
            self.write('decompile/tools/capture.lua', b'local unreviewed_payload={1,2,3}\n')
            self.write('tools/portable/source_data_manifest.json', b'{}')
            return original_stage(root, tree, manifest)
        with patch.object(source_data, 'stage', side_effect=changed_after_snapshot):
            exporter.export(self.root, self.base/'snapshot')
        lua = (self.base/'snapshot/decompile/tools/capture.lua').read_text()
        self.assertIn('os.getenv', lua)
        self.assertIn('reviewed=true', lua)
        self.assertNotIn('unreviewed_payload', lua)
        self.assertTrue(public_review.review(self.base/'snapshot')['passed'])

    def test_private_candidate_keeps_invalid_policy_but_not_unreviewed_inputs(self):
        self.write(public_review.POLICY_PATH, b'{"schema": true}')
        self.refresh_inputs()
        result = exporter.export(self.root, self.base/'invalid-policy', require_public_clean=False)
        self.assertFalse(result['public_content_review_passed'])
        self.write('decompile/tools/capture.lua', b'changed after input review')
        with self.assertRaisesRegex(ValueError, 'Reviewed public export input changed'):
            exporter.export(self.root, self.base/'unreviewed', require_public_clean=False)


if __name__=='__main__':unittest.main()
