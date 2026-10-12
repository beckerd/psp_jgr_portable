"""Fresh-checkout source audit: no private originals or existing Git needed."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import public_review
import source_check


class SourceCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / 'fresh source'
        self.root.mkdir()
        self.write('psp/Makefile', 'all:\n\t@echo synthetic build\n')
        self.write('psp/src/main.c', 'int main(void) { return 0; }\n')
        self.write('.gitignore', '/build/\n/psp/*.o\n/psp/data/\n')
        self.write('tools/portable/public_sources.json', json.dumps(dict(schema=1, files=[])))
        self.admit()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)
        return path

    def admit(self):
        rows = public_review.inventory(self.root)
        self.write(public_review.POLICY_PATH, json.dumps(dict(schema=1,
            whole_tree_review_complete=True, findings=[], reviewed_files=rows)))

    def git(self, *args):
        return source_check.git(list(args), cwd=self.root)

    def test_fresh_tree_ignores_generated_outputs_and_preserves_exact_source(self):
        self.write('build/ROM/disc.gdi', 'private synthetic disc')
        self.write('psp/main.o', 'synthetic object')
        self.write('psp/data/assets/private.bin', 'private synthetic asset')
        out = self.base / 'checked'
        report = source_check.check(self.root, out)
        self.assertTrue(report['passed'])
        self.assertTrue(public_review.review(out)['passed'])
        self.assertFalse((self.root / '.git').exists())
        self.assertFalse((out / 'build').exists())
        self.assertFalse((out / 'psp/data').exists())
        self.assertFalse((out / 'psp/Makefile.portable').exists())
        self.assertEqual((out / 'psp/src/main.c').read_bytes(), (self.root / 'psp/src/main.c').read_bytes())
        self.assertEqual((out / public_review.POLICY_PATH).read_bytes(), (self.root / public_review.POLICY_PATH).read_bytes())
        receipt = json.loads((out / 'EXPORT-REVIEW.json').read_text())
        self.assertFalse(receipt['compiled_data_separation_verified'])
        self.assertFalse(receipt['distribution_ready'])
        self.assertIsNone(receipt['build_audit'])

    def test_initialized_repo_does_not_copy_history_and_rejects_forced_asset(self):
        self.git('init', '--quiet', '--template=')
        self.git('add', '.')
        self.write('build/track.bin', 'synthetic original bytes')
        self.assertTrue(source_check.check(self.root)['passed'])
        self.git('add', '-f', 'build/track.bin')
        with self.assertRaisesRegex(ValueError, 'build/track.bin'):
            source_check.check(self.root)

    def test_unreviewed_text_asset_and_nested_ignore_are_rejected(self):
        for name in ('unexpected.c', 'track.bin', 'secret/.gitignore'):
            with self.subTest(name=name):
                path = self.write(name, 'unreviewed')
                with self.assertRaisesRegex(ValueError, 'Unreviewed tracked or nonignored'):
                    source_check.check(self.root)
                path.unlink()

    def test_changed_missing_and_symlinked_sources_fail_without_output(self):
        path = self.root / 'psp/src/main.c'
        original = path.read_bytes()
        out = self.base / 'failed'
        path.write_text('int changed;\n')
        with self.assertRaisesRegex(ValueError, 'Reviewed source changed'):
            source_check.check(self.root, out)
        self.assertFalse(out.exists())
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or nonregular'):
            source_check.check(self.root)
        external = self.base / 'external.c'; external.write_bytes(original)
        path.symlink_to(external)
        with self.assertRaisesRegex(ValueError, 'Linked source'):
            source_check.check(self.root)

    def test_links_in_reviewed_parent_or_destination_are_rejected(self):
        (self.root / 'psp/src').rename(self.base / 'src')
        (self.root / 'psp/src').symlink_to(self.base / 'src', target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Linked source'):
            source_check.check(self.root)
        (self.root / 'psp/src').unlink()
        (self.base / 'src').rename(self.root / 'psp/src')
        (self.base / 'output-link').symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'parents must not be links'):
            source_check.check(self.root, self.base / 'output-link/snapshot')

    def test_bound_text_payload_extension_still_fails_content_review(self):
        self.write('disc.bin', 'synthetic ASCII payload')
        # Deliberately adversarial policy cannot bypass the suffix/binary gate.
        policy = json.loads((self.root / public_review.POLICY_PATH).read_text())
        data = (self.root / 'disc.bin').read_bytes()
        policy['reviewed_files'].append(dict(path='disc.bin', bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        self.write(public_review.POLICY_PATH, json.dumps(policy))
        with self.assertRaisesRegex(ValueError, 'Asset/build output'):
            source_check.check(self.root)

    def test_inherited_git_environment_and_outer_checkout_do_not_select_inputs(self):
        subprocess.run(['git', 'init', '--quiet', '--template=', str(self.base)], check=True)
        (self.base / 'outside.bin').write_bytes(b'private')
        with patch.dict(os.environ, {'GIT_DIR': str(self.base / '.git'), 'GIT_WORK_TREE': str(self.base)}):
            self.assertTrue(source_check.check(self.root)['passed'])

    def test_existing_destination_and_stale_receipts_are_rejected(self):
        out = self.base / 'existing'; out.mkdir()
        with self.assertRaisesRegex(ValueError, 'must be new'):
            source_check.check(self.root, out)
        self.write('EXPORT-REVIEW.json', '{}')
        with self.assertRaisesRegex(ValueError, 'EXPORT-REVIEW.json'):
            source_check.check(self.root)

    def test_optional_identity_is_only_exact_catalog_metadata_and_reviewed_hash(self):
        import package
        catalog = dict(files=[dict(path='synthetic.jsd', bytes=4, crc32=123)])
        self.write('tools/portable/asset_catalog.json', json.dumps(catalog))
        self.write('tools/portable/native_replay_identity.json', '{}')
        identity = self.root / 'psp/src/portable_asset_identity.h'
        package.write_identity(package.asset_manifest(catalog['files']), 1, identity)
        self.admit()
        generated = self.base / 'generated.h'; generated.write_bytes(identity.read_bytes())
        identity.unlink()
        # Native generator fingerprinting is covered by replay_identity's own
        # tests. Exercise actual catalog -> header serialization here.
        with patch('replay_identity.select_catalog', return_value=catalog) as select:
            self.assertTrue(source_check.check(self.root, identity=generated)['passed'])
            select.assert_called_once_with(catalog, 'native-v1', {}, root=self.root)
        generated.write_text('/* unreviewed hidden content */\n')
        with patch('replay_identity.select_catalog', return_value=catalog):
            with self.assertRaisesRegex(ValueError, 'differs from native-v1'):
                source_check.check(self.root, identity=generated)
        with patch('replay_identity.select_catalog', side_effect=ValueError('stale generator')):
            with self.assertRaisesRegex(ValueError, 'stale generator'):
                source_check.check(self.root, identity=generated)


if __name__ == '__main__':
    unittest.main()
