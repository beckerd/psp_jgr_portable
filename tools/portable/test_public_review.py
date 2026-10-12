import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import public_review
from public_review import review, require_clean, inventory, POLICY_PATH


class PublicReview(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / 'game.c').write_text('int example(void) { return 0; }\n')
        self.policy = dict(schema=1, whole_tree_review_complete=True, findings=[], reviewed_files=inventory(self.root))

    def tearDown(self):
        self.temp.cleanup()

    def finding(self, category='original_asset_data'):
        self.policy['findings'].append(dict(id='example', category=category,
            path='game.c', status='open', evidence='Synthetic fixture'))

    def test_text_and_bss_success_never_close_original_content_findings(self):
        self.finding()
        report = review(self.root, self.policy)
        self.assertFalse(report['passed'])
        self.assertEqual(report['inventory_failures'], [])
        self.assertFalse(report['original_asset_absence_verified'])
        with self.assertRaisesRegex(ValueError, 'example'):
            require_clean(report)
        (self.root / 'game.c').write_text('int changed;\n')
        self.assertIn('example', review(self.root, self.policy)['blockers'])
        (self.root / 'game.c').unlink()
        self.assertIn('example', review(self.root, self.policy)['blockers'])

    def test_resolution_is_bound_to_reviewed_replacement(self):
        self.finding()
        sha = review(self.root, self.policy)['findings'][0]['current_sha256']
        self.policy['findings'][0].update(status='resolved', resolution=dict(disposition='replaced', sha256=sha, evidence='Reviewed replacement'))
        self.assertTrue(review(self.root, self.policy)['passed'])
        (self.root / 'game.c').write_text('int changed;\n')
        report = review(self.root, self.policy)
        self.assertFalse(report['passed'])
        self.assertTrue(report['findings'][0]['resolution_stale'])

    def test_reconstructed_code_policy_is_separate_from_asset_policy(self):
        self.finding('translated_game_code')
        report = review(self.root, self.policy)
        self.assertTrue(report['passed'])
        self.assertTrue(report['known_translated_code_present'])
        strict = review(self.root, self.policy, strict_code=True)
        self.assertFalse(strict['passed'])
        self.assertTrue(strict['original_asset_absence_verified'])

    def test_retained_translation_is_present_and_strict_blocker(self):
        self.finding('translated_game_code')
        sha = self.policy['reviewed_files'][0]['sha256']
        self.policy['findings'][0].update(status='resolved', resolution=dict(disposition='retained_code', sha256=sha, evidence='Accepted under reconstructed-code policy'))
        self.assertTrue(review(self.root, self.policy)['passed'])
        strict = review(self.root, self.policy, strict_code=True)
        self.assertFalse(strict['passed'])
        self.assertTrue(strict['known_translated_code_present'])
        self.assertEqual(strict['policy'], 'reject-known-translated-programs')

    def test_removed_resolution_cannot_match_a_present_file(self):
        self.finding()
        self.policy['findings'][0].update(status='resolved', resolution=dict(disposition='removed', sha256=None, evidence='Removed diagnostic'))
        self.assertFalse(review(self.root, self.policy)['passed'])
        (self.root/'game.c').unlink()
        (self.root/'replacement.c').write_text('int replacement;\n')
        self.policy['reviewed_files'] = inventory(self.root)
        self.assertTrue(review(self.root, self.policy)['passed'])

    def test_missing_empty_and_binary_receipt_are_rejected(self):
        with self.assertRaises(ValueError): review(self.root / 'absent', self.policy)
        (self.root / 'game.c').unlink()
        self.assertFalse(review(self.root, self.policy)['passed'])
        (self.root / 'game.c').write_text('int example;\n')
        (self.root / 'EXPORT-REVIEW.json').write_bytes(b'\x00\xff')
        self.assertFalse(review(self.root, self.policy)['passed'])

    def test_incomplete_review_cannot_pass_even_without_known_findings(self):
        self.policy['whole_tree_review_complete'] = False
        self.assertIn('whole-export-content-review-incomplete', review(self.root, self.policy)['blockers'])

    def test_binary_history_links_and_asset_extensions_are_rejected(self):
        for name, data in [('payload.c', b'\x00\xff'), ('asset.bin', b'ASCII'), ('.git/config', b'git')]:
            with self.subTest(name=name):
                p = self.root / name
                p.parent.mkdir(exist_ok=True)
                p.write_bytes(data)
                self.assertFalse(review(self.root, self.policy)['passed'])
                p.unlink()
                if p.parent != self.root:
                    p.parent.rmdir()
        (self.root / 'link.c').symlink_to(self.root / 'game.c')
        self.assertFalse(review(self.root, self.policy)['passed'])

    def test_exact_project_artwork_still_requires_reviewed_inventory(self):
        payload = b'\x89PNG\r\n\x1a\n\x00\xffsynthetic test only'
        name = 'icon/ICON0.png'
        path = self.root / name; path.parent.mkdir(); path.write_bytes(payload)
        with patch.dict(public_review.PROJECT_ARTWORK, {name: hashlib.sha256(payload).hexdigest()}):
            report = review(self.root, self.policy)
            self.assertFalse(report['passed'])
            self.assertIn('Unreviewed export file: ' + name, report['inventory_failures'])
            self.policy['reviewed_files'] = inventory(self.root)
            self.assertTrue(review(self.root, self.policy)['passed'])
            # A byte change is refused even if someone refreshes the ordinary
            # source inventory. The artwork exception has its own exact hash.
            path.write_bytes(payload + b'changed')
            row = next(row for row in self.policy['reviewed_files'] if row['path'] == name)
            row.update(bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            report = review(self.root, self.policy)
            self.assertFalse(report['passed'])
            self.assertIn('Project artwork identity changed: ' + name, report['inventory_failures'])

    def test_project_artwork_does_not_admit_other_paths_or_pngs(self):
        payload = b'\x89PNG\r\n\x1a\n\x00\xffsynthetic test only'
        with patch.dict(public_review.PROJECT_ARTWORK,
                        {'icon/ICON0.png': hashlib.sha256(payload).hexdigest()}):
            for name in ('ICON0.png', 'icon/icon0.png', 'icon/OTHER.png', 'textures/game.png'):
                with self.subTest(name=name):
                    path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(payload)
                    files, failures, _ = public_review.scan(self.root)
                    self.assertIn('Asset/build output in source export: ' + name, failures)
                    self.assertTrue(any('Non-text' in failure or 'Binary bytes' in failure for failure in failures))
                    path.unlink()
            path = self.root / 'icon/PIC1.png'; path.write_bytes(payload)
            _, failures, _ = public_review.scan(self.root)
            self.assertIn('Project artwork identity changed: icon/PIC1.png', failures)

    def test_reapproved_literal_texmap_spec_is_still_rejected(self):
        name = 'decompile/example_spec.txt'
        path = self.root / name; path.parent.mkdir()
        source_binding = 'Example:input.dat:textures.dat:0,0,0,0:texbinding=1,1000,1020,2,2000,3\n'
        path.write_text(source_binding)
        self.policy['reviewed_files'] = inventory(self.root)
        row = next(row for row in self.policy['reviewed_files'] if row['path'] == name)
        finding = dict(id='spec-mapping', category='original_asset_data', path=name,
                       status='resolved', evidence='Synthetic decoded map regression')
        self.policy['findings'].append(finding)
        for literal in ('texmap=0', 'texmap=7:lighting=1', 'texmap=3,2',
                        'texmap = 1, +2, -3 :lighting=1', 'texmap=0x1,0x02'):
            with self.subTest(literal=literal):
                path.write_text('# Synthetic fixture\nExample:input.dat:textures.dat:0,0,0,0:' + literal + '\n')
                data = path.read_bytes()
                row.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                finding['resolution'] = dict(disposition='replaced', sha256=row['sha256'],
                                             evidence='Rehashed without changing the decoded data')
                report = review(self.root, self.policy)
                failure = 'Decoded literal texmap mapping in public spec: ' + name + ':2'
                self.assertEqual(report['inventory_failures'], [failure])
                self.assertFalse(report['passed'])
                self.assertFalse(report['original_asset_absence_verified'])
                self.assertEqual(report['blockers'], [])
                with self.assertRaisesRegex(ValueError, 'Decoded literal texmap'):
                    require_clean(report)
                with self.assertRaisesRegex(ValueError, 'Decoded literal texmap'):
                    inventory(self.root)

    def test_source_texbindings_and_test_literals_are_allowed(self):
        path = self.root / 'decompile/example_spec.txt'; path.parent.mkdir()
        path.write_text('Example:input.dat:textures.dat:0,0,0,0:texbinding=1,1000,1020,2,2000,3\n')
        (self.root / 'test_converter.py').write_text('synthetic_spec = "Example:texmap=1,2"\n')
        self.policy['reviewed_files'] = inventory(self.root)
        self.assertTrue(review(self.root, self.policy)['passed'])

    def test_inventory_hash_changes_with_any_tool_or_document(self):
        before = review(self.root, self.policy)['source_tree_sha256']
        (self.root / 'tool.py').write_text('print("synthetic")\n')
        self.assertNotEqual(before, review(self.root, self.policy)['source_tree_sha256'])
        self.assertFalse(review(self.root, self.policy)['passed'])
        self.policy['reviewed_files'] = inventory(self.root)
        self.assertTrue(review(self.root, self.policy)['passed'])
        (self.root / 'game.c').write_text('int newly_embedded_payload;\n')
        self.assertFalse(review(self.root, self.policy)['passed'])
        self.policy['reviewed_files'] = inventory(self.root)
        (self.root / 'tool.py').unlink()
        self.assertFalse(review(self.root, self.policy)['passed'])

    def test_target_policy_and_receipt_binding(self):
        path = self.root/POLICY_PATH
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(self.policy))
        report = review(self.root)
        self.assertTrue(report['passed'])
        receipt = self.root/'PUBLIC-CONTENT-REVIEW.json'
        receipt.write_text(json.dumps(report))
        self.assertTrue(review(self.root)['passed'])
        self.assertTrue(review(self.root, strict_code=True)['passed'])
        report['extra_original_table'] = [1, 2, 3]
        receipt.write_text(json.dumps(report))
        self.assertFalse(review(self.root)['passed'])
        receipt.write_text('null')
        self.assertFalse(review(self.root)['passed'])
        receipt.unlink()
        self.policy['whole_tree_review_complete'] = False
        self.assertFalse(review(self.root, self.policy)['passed'])

    def test_schema_and_disposition_types_are_explicit(self):
        original = copy.deepcopy(self.policy)
        for value in (True, 1.0, '1'):
            self.policy['schema'] = value
            with self.assertRaises(ValueError): review(self.root, self.policy)
        self.policy = copy.deepcopy(original)
        self.policy.pop('reviewed_files')
        self.assertFalse(review(self.root, self.policy)['passed'])
        self.policy = copy.deepcopy(original)
        self.finding()
        sha = self.policy['reviewed_files'][0]['sha256']
        for resolution in ({'sha256':sha, 'evidence':'No disposition'},
                           {'sha256':None, 'evidence':'Replacement must be hashed', 'disposition':'replaced'},
                           {'sha256':sha, 'evidence':'Asset data cannot be retained', 'disposition':'retained_code'}):
            self.policy['findings'][0].update(status='resolved', resolution=resolution)
            with self.assertRaises(ValueError): review(self.root, self.policy)

    def test_unsafe_duplicate_or_unbound_findings_fail(self):
        self.finding()
        original = copy.deepcopy(self.policy)
        self.policy['findings'][0]['path'] = '../outside'
        with self.assertRaises(ValueError): review(self.root, self.policy)
        self.policy = copy.deepcopy(original)
        self.policy['findings'].append(self.policy['findings'][0])
        with self.assertRaises(ValueError): review(self.root, self.policy)
        self.policy = copy.deepcopy(original)
        self.policy['findings'][0]['status'] = 'resolved'
        with self.assertRaises(ValueError): review(self.root, self.policy)


if __name__ == '__main__':
    unittest.main()
