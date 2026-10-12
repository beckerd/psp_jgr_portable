import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zlib
import struct
from PIL import Image

import package


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.payload = b'synthetic asset\x00\xff'
        (self.source / 'synthetic.bin').write_bytes(self.payload)
        self.catalog = {'source': 'user-disc', 'complete': True, 'files': [{
            'path': 'synthetic.bin', 'bytes': len(self.payload),
            'sha256': hashlib.sha256(self.payload).hexdigest()}]}

    def test_pack_identity_and_installation(self):
        out, header = self.root / 'assets', self.root / 'identity.h'
        report = package.stage_assets(self.source, self.catalog, out, header)
        manifest = (out / package.MANIFEST_NAME).read_bytes()
        self.assertIn('0x%08xu' % (zlib.crc32(manifest) & 0xffffffff), header.read_text())
        self.assertEqual((out / 'synthetic.bin').read_bytes(), self.payload)
        build = self.root / 'build'
        build.mkdir()
        for name in package.EXECUTABLES | {'crash_handler_license.txt'}:
            (build / name).write_text('synthetic ' + name)
        image = self.root / 'image'
        result = package.install_image(build, out, report, image)
        self.assertFalse(result['public_distribution'])
        self.assertEqual({p.name for p in (image / 'data').iterdir()}, {'assets', 'cache', 'log'})
        self.assertTrue((image / 'data/log').is_dir())
        self.assertEqual((image / 'data/assets/synthetic.bin').read_bytes(), self.payload)
        self.assertFalse((image / 'synthetic.bin').exists())

    def test_identity_and_recipe_agree_without_asset_files(self):
        from install_plan import make_plan, HEADER
        self.catalog['files'][0]['crc32'] = zlib.crc32(self.payload) & 0xffffffff
        (self.source / 'synthetic.bin').unlink()
        header = self.root / 'identity.h'
        identity = package.identity_from_catalog(self.catalog, header)
        plan, _ = make_plan(self.catalog, {'synthetic.bin': 'decompile/extracted/SYNTH.BIN'}, conversions={})
        self.assertEqual(HEADER.unpack_from(plan)[6], int(identity['manifest_crc32'], 16))
        self.assertIn('JGR_ASSET_FILE_COUNT 1u', header.read_text())
        with self.assertRaisesRegex(ValueError, 'identity'):
            make_plan(self.catalog, {}, conversions={}, manifest_crc=123)

    def test_identity_rejects_missing_checksum_metadata(self):
        with self.assertRaisesRegex(ValueError, 'CRC32'):
            package.identity_from_catalog(self.catalog, self.root / 'identity.h')

    def test_incomplete_disc_pipeline_cannot_be_certified(self):
        self.catalog['complete'] = False
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            package.stage_assets(self.source, self.catalog, self.root / 'assets')
        self.assertFalse((self.root / 'assets').exists())

    def test_private_reference_explicit_and_never_complete(self):
        self.catalog.pop('source')
        with self.assertRaisesRegex(ValueError, 'user-disc'):
            package.stage_assets(self.source, self.catalog, self.root / 'assets')
        report = package.stage_assets(self.source, self.catalog, self.root / 'assets', reference=True)
        self.assertTrue(report['reference_only'])
        self.assertFalse(report['complete'])

    def test_traversal_and_duplicate_catalog_rejected(self):
        for path in ('../a', '/a', './a', 'a//b', 'a/../b', 'ms0:/a', 'a\\b', 'a\nb'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                package.safe_name(path)
        self.catalog['files'] *= 2
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            package.stage_assets(self.source, self.catalog, self.root / 'assets')

    def test_manifest_names_match_runtime_contract(self):
        for name in ('a' * 192, 'unicode_é.bin', 'has+plus.bin', 'data/assets/a.bin',
                     'jgr_log.txt', 'jgr_story.sav', 'shot.raw', 'autopilot'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                package.asset_manifest([{'path': name, 'bytes': 0, 'crc32': 0}])

    def test_modified_asset_rejected_and_partial_removed(self):
        (self.source / 'synthetic.bin').write_bytes(b'corruption')
        with self.assertRaisesRegex(ValueError, 'differs'):
            package.stage_assets(self.source, self.catalog, self.root / 'assets')
        self.assertFalse((self.root / 'assets').exists())
        self.assertFalse(list(self.root.glob('assets.partial-*')))

    def test_generated_asset_cannot_escape_root_through_symlink(self):
        (self.root / 'outside.bin').write_bytes(self.payload)
        (self.source / 'synthetic.bin').unlink()
        (self.source / 'synthetic.bin').symlink_to(self.root / 'outside.bin')
        with self.assertRaisesRegex(ValueError, 'escapes'):
            package.stage_assets(self.source, self.catalog, self.root / 'assets')

    def test_cache_mutation_between_pack_and_install_is_detected(self):
        out = self.root / 'assets'
        report = package.stage_assets(self.source, self.catalog, out)
        (out / 'synthetic.bin').write_bytes(b'changed')
        build = self.root / 'build'
        build.mkdir()
        for name in package.EXECUTABLES | {'crash_handler_license.txt'}:
            (build / name).write_text('synthetic')
        with self.assertRaisesRegex(ValueError, 'asset mismatch'):
            package.install_image(build, out, report, self.root / 'image')
        self.assertFalse((self.root / 'image').exists())

    def test_optional_background_is_separate_from_verified_assets(self):
        assets = self.root / 'assets'
        catalog = package.stage_assets(self.source, self.catalog, assets)
        original_manifest = (assets / package.MANIFEST_NAME).read_bytes()
        build = self.root / 'build'
        build.mkdir()
        for name in package.EXECUTABLES | {'crash_handler_license.txt'}:
            (build / name).write_text('synthetic')
        source_png = self.root / 'setup.png'
        Image.new('RGB', (480, 272), 'red').save(source_png)
        destination = self.root / 'image'
        report = package.install_image(build, assets, catalog, destination, source_png)
        self.assertEqual((destination / 'data/assets/assets.manifest').read_bytes(), original_manifest)
        self.assertEqual((destination / 'data/setup/background.jbg').stat().st_size, 261144)
        self.assertFalse((destination / 'data/assets/background.jbg').exists())
        self.assertFalse(report['setup_background']['verified_game_asset'])
        self.assertEqual(report['setup_background']['source_name'], 'setup.png')
        self.assertIn('data/setup/background.jbg', [row['path'] for row in report['files']])
        self.assertEqual(json.loads((destination / 'installation.json').read_text()), report)

    def test_invalid_background_removes_partial_installation(self):
        assets = self.root / 'assets'
        catalog = package.stage_assets(self.source, self.catalog, assets)
        build = self.root / 'build'
        build.mkdir()
        for name in package.EXECUTABLES | {'crash_handler_license.txt'}:
            (build / name).write_text('synthetic')
        source_png = self.root / 'setup.png'
        Image.new('RGB', (1, 1)).save(source_png)
        with self.assertRaisesRegex(ValueError, '480x272'):
            package.install_image(build, assets, catalog, self.root / 'image', source_png)
        self.assertFalse((self.root / 'image').exists())
        self.assertFalse(list(self.root.glob('image.partial-*')))


class StarterTests(unittest.TestCase):
    def setUp(self):
        import bundle
        from install_plan import make_plan
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        prx = bytearray(64)
        prx[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HH', prx, 16, 0xffa0, 8)
        struct.pack_into('<H', prx, 40, 52)
        (self.build / 'module.prx').write_bytes(prx)
        (self.build / 'notice.txt').write_text('Synthetic test notice')
        catalog = {'files': [{'path': 'synthetic.bin', 'bytes': 1,
                             'sha256': hashlib.sha256(b'x').hexdigest()}]}
        self.plan, self.coverage = make_plan(catalog, {'synthetic.bin': 'decompile/extracted/SYNTH.BIN'}, conversions={})
        (self.build / 'recipe').write_bytes(self.plan)
        payload, self.bundle = bundle.build(self.build / 'module.prx', self.build / 'module.prx',
            self.build / 'notice.txt', self.build / 'recipe', self.build / 'module.prx')
        self.data = struct.pack('<4sI8I', b'\0PBP', 0x10000, 40, 44, 44, 44, 44, 44, 44, 108) + b'SFO!' + prx + payload
        (self.build / 'EBOOT.PBP').write_bytes(self.data)
        (self.build / '.setup').mkdir()
        (self.build / '.setup/build-receipt.json').write_text(json.dumps({
            'eboot_sha256': hashlib.sha256(self.data).hexdigest(), 'bundle': self.bundle}))
        self.destination = self.root / 'JGR_PORTABLE'

    def package(self, plan=None, complete=True, **options):
        with patch('install_plan.make_plan', return_value=(self.plan if plan is None else plan,
                                                          {**self.coverage, 'complete': complete})):
            return package.starter_image(self.build, self.destination, **options)

    def test_only_eboot_is_staged(self):
        (self.build / 'private-disc.gdi').write_text('must not copy')
        result = self.package()
        self.assertEqual([p.name for p in self.destination.iterdir()], ['EBOOT.PBP'])
        self.assertEqual((self.destination / 'EBOOT.PBP').read_bytes(), self.data)
        self.assertFalse(result['public_distribution'])

    def test_incomplete_or_stale_recipe_never_staged(self):
        for plan, complete in ((self.plan, False), (self.plan + b'x', True)):
            with self.subTest(complete=complete), self.assertRaises(ValueError):
                self.package(plan, complete)
            self.assertFalse(self.destination.exists())

    def test_build_receipt_must_match_the_executable(self):
        (self.build / '.setup/build-receipt.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package()
        self.assertFalse(self.destination.exists())

    def test_invalid_container_never_staged(self):
        bad = bytearray(self.data)
        struct.pack_into('<I', bad, 12, 43)  # Unreviewed ICON0 content.
        (self.build / 'EBOOT.PBP').write_bytes(bad)
        with self.assertRaisesRegex(ValueError, 'artwork'):
            self.package()
        self.assertFalse(self.destination.exists())

    def test_private_artwork_preserves_program_bundle_and_source(self):
        icon, picture = self.root / 'icon.png', self.root / 'picture.png'
        Image.new('RGB', (144, 80), 'green').save(icon)
        Image.new('RGB', (480, 272), 'blue').save(picture)
        result = self.package(icon0=icon, pic1=picture, setup_background=picture)
        packed = (self.destination / 'EBOOT.PBP').read_bytes()
        offsets = struct.unpack_from('<8I', packed, 8)
        before = struct.unpack_from('<8I', self.data, 8)
        self.assertEqual(packed[offsets[0]:offsets[1]], self.data[before[0]:before[1]])
        self.assertEqual(packed[offsets[1]:offsets[2]], icon.read_bytes())
        self.assertEqual(packed[offsets[4]:offsets[5]], picture.read_bytes())
        self.assertEqual(packed[offsets[6]:], self.data[before[6]:])
        self.assertEqual((self.build / 'EBOOT.PBP').read_bytes(), self.data)
        self.assertEqual([item['slot'] for item in result['private_artwork']], ['ICON0', 'PIC1'])
        self.assertFalse(result['public_distribution'])
        self.assertEqual(result['source_eboot_sha256'], hashlib.sha256(self.data).hexdigest())
        self.assertEqual((self.destination / 'data/setup/background.jbg').stat().st_size, 261144)
        self.assertFalse((self.destination / 'data/assets').exists())

    def test_packaged_artwork_requires_explicit_opt_in_again(self):
        from build_device_setup import with_artwork
        icon = self.root / 'icon.png'
        Image.new('RGB', (144, 80)).save(icon)
        data, _ = with_artwork(self.data, icon0=icon)
        (self.build / 'EBOOT.PBP').write_bytes(data)
        (self.build / '.setup/build-receipt.json').write_text(json.dumps({
            'eboot_sha256': hashlib.sha256(data).hexdigest(), 'bundle': self.bundle}))
        with self.assertRaisesRegex(ValueError, 'artwork'):
            self.package()
        self.package(icon0=icon)
        self.assertEqual((self.destination / 'EBOOT.PBP').read_bytes(), data)

    def test_invalid_or_linked_private_artwork_is_rejected(self):
        icon = self.root / 'icon.png'
        Image.new('RGB', (1, 1)).save(icon)
        with self.assertRaisesRegex(ValueError, '144x80'):
            self.package(icon0=icon)
        linked = self.root / 'linked.png'
        linked.symlink_to(icon)
        with self.assertRaisesRegex(ValueError, 'linked'):
            self.package(icon0=linked)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob('JGR_PORTABLE.partial-*')))

    def test_bad_private_background_removes_partial_starter(self):
        picture = self.root / 'picture.png'
        Image.new('RGB', (1, 1)).save(picture)
        with self.assertRaisesRegex(ValueError, '480x272'):
            self.package(setup_background=picture)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob('JGR_PORTABLE.partial-*')))


if __name__ == '__main__':
    unittest.main()
