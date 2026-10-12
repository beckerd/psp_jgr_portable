"""Matching-source ZIPs use reviewed bytes and bind actual compile inputs."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import source_bundle
import source_check
import public_review


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='matching source ')
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / 'source'; self.root.mkdir()
        self.write('.gitignore', '/build/\n/psp/data/\n')
        self.write('psp/src/main.c', '#include "value.h"\nint main(void) { return VALUE; }\n')
        self.write('psp/src/value.h', '#define VALUE 0\n')
        self.identity = self.write('psp/src/portable_asset_identity.h', '/* synthetic checksum metadata */\n')
        self.write('tools/portable/public_sources.json', '{"schema":1,"files":[]}\n')
        for name in source_bundle.GUIDES:
            self.write(name, 'Build guide\n')
        for name in source_bundle.LICENSE_FILES:
            self.write(name, 'License notice\n')
        rows = public_review.inventory(self.root)
        self.write(public_review.POLICY_PATH, json.dumps(dict(schema=1,
            whole_tree_review_complete=True, findings=[], reviewed_files=rows)))
        # Keep the inventory/content checks real. The catalog-to-header check
        # has dedicated source_check tests; this fixture has no game catalog.
        self.identity_check = patch('source_check.checked_identity', side_effect=lambda root, path: Path(path).read_bytes())
        self.identity_check.start()

    def tearDown(self):
        self.identity_check.stop()
        self.temp.cleanup()

    def write(self, name, data):
        path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data)
        return path

    def release(self):
        return source_bundle.SourceRelease(self.root, self.identity, 'emcc 6.0.12 test')

    def test_deterministic_archive_exact_inventory_and_rebuildable_checkout(self):
        self.write('build/ROM/disc.gdi', 'synthetic private disc')
        self.write('psp/data/assets/private.bin', 'synthetic private asset')
        source_check.git(['init', '--quiet', '--template='], cwd=self.root)
        first = self.release()
        os.utime(self.root / 'psp/src/main.c', (50, 50))
        second = self.release()
        self.assertEqual(first.data, second.data)
        self.assertEqual(first.metadata['archive_sha256'], hashlib.sha256(first.data).hexdigest())
        self.assertIn(first.metadata['archive_sha256'], first.metadata['archive_filename'])
        with zipfile.ZipFile(io.BytesIO(first.data)) as bundle:
            names = bundle.namelist()
            self.assertFalse(any('/.git/' in name or '/build/' in name or '/data/' in name for name in names))
            manifest = json.loads(bundle.read('SOURCE-RELEASE.json'))
            self.assertEqual(len(manifest['files']), len(first.files))
            self.assertEqual(manifest['license_files'], ['LICENSE', 'THIRD_PARTY_NOTICES.md'])
            self.assertNotIn(str(self.base), bundle.read('SOURCE-RELEASE.json').decode())
            for row in manifest['files']:
                data = bundle.read('jgr-portable-source/' + row['path'])
                self.assertEqual(data, first.files[row['path']])
                self.assertEqual(row['sha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(row['bytes'], len(data))
            bundle.extractall(self.base / 'unpacked')
        rebuilt = self.base / 'unpacked/jgr-portable-source'
        self.assertTrue(source_check.check(rebuilt)['passed'])

    def test_forced_tracked_private_file_and_changed_reviewed_source_fail(self):
        source_check.git(['init', '--quiet', '--template='], cwd=self.root)
        source_check.git(['add', '.'], cwd=self.root)
        self.write('build/track.bin', 'synthetic private data')
        source_check.git(['add', '-f', 'build/track.bin'], cwd=self.root)
        with self.assertRaisesRegex(ValueError, 'build/track.bin'):
            self.release()

    def test_external_psp_must_match_both_source_and_header(self):
        release = self.release()
        psp = self.base / 'external psp'
        (psp / 'src').mkdir(parents=True)
        for name in ('main.c', 'value.h'):
            path = psp / 'src' / name
            path.write_bytes((self.root / 'psp/src' / name).read_bytes())
        release.bind([psp / 'src/main.c', psp / 'src/value.h'], psp)
        for name in ('main.c', 'value.h'):
            path = psp / 'src' / name; data = path.read_bytes(); path.write_bytes(data + b'/* altered */')
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'differs from matching source'):
                release.bind([path], psp)
            path.write_bytes(data)
        outside = self.base / 'outside.h'; outside.write_text('int outside;')
        with self.assertRaisesRegex(ValueError, 'outside matching source'):
            release.bind([outside], psp)

    def test_dependencies_with_spaces_and_late_source_mutation(self):
        release = self.release()
        obj = self.base / 'with spaces.o'; obj.write_bytes(b'object')
        paths = [self.root / 'psp/src/main.c', self.root / 'psp/src/value.h']
        escape = lambda value: str(value).replace(' ', '\\ ')
        obj.with_suffix('.d').write_text(escape(obj) + ': \\\n ' + ' '.join(map(escape, paths)) + '\n')
        self.assertEqual(release.bind_dependencies([obj], self.root / 'psp'), 2)
        release.unchanged()
        self.write('README.md', 'changed after compilation\n')
        with self.assertRaisesRegex(ValueError, 'Reviewed source changed'):
            release.unchanged()

    def test_missing_guide_and_linked_compiler_input_fail(self):
        release = self.release()
        files = dict(release.files); del files['README.md']
        with self.assertRaisesRegex(ValueError, 'Missing source build guide'):
            source_bundle.archive(files, {}, 'a'*64, 'emcc 6.0.12')
        linked = self.base / 'linked'; linked.symlink_to(self.root / 'psp', target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Linked compiler input'):
            release.bind([linked / 'src/main.c'], self.root / 'psp')

    def test_content_cache_changes_even_if_source_timestamp_is_restored(self):
        source = self.root / 'psp/src/main.c'
        timestamp = source.stat().st_mtime_ns
        before = source_bundle.content_fingerprint({'main.c': source.read_bytes()})
        source.write_text('int main(void) { return 1; }\n')
        os.utime(source, ns=(timestamp, timestamp))
        self.assertEqual(source.stat().st_mtime_ns, timestamp)
        after = source_bundle.content_fingerprint({'main.c': source.read_bytes()})
        self.assertNotEqual(before, after)

    def test_missing_or_empty_project_license_fails(self):
        release = self.release()
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            for bad in (None, b'', b' \n'):
                files = dict(release.files)
                if bad is None:
                    del files[name]
                else:
                    files[name] = bad
                with self.subTest(name=name, bad=bad), self.assertRaisesRegex(ValueError, 'source license notice'):
                    source_bundle.archive(files, {}, 'a'*64, 'emcc 6.0.12')


if __name__ == '__main__':
    unittest.main()
