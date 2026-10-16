import unittest
from pathlib import Path
import tempfile
import hashlib
import json
import struct
from unittest.mock import patch
from audit_build import symbols, check_storage, check_setup_storage, source_binding, audit_split, digest, pbp_parts
import bundle
from build_device_setup import setup_sources


class SetupSourceClosure(unittest.TestCase):
    def test_absent_exported_legacy_include_is_not_an_admitted_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            psp=Path(temporary)/'psp';source=psp/'src';source.mkdir(parents=True)
            for name in ('source_fmac.h','source_float_fast.h','stage_texture_usage.inc','crash.c','release_report.c'):
                (source/name).write_text('/* authored test header */\n')
            launcher=source/'portable_launcher.c'
            launcher.write_text('#if !JGR_PORTABLE_ASSETS\n#include "generated/source_data/legacy.inc"\n#endif\n')
            units,files,tables=setup_sources(psp)
            self.assertEqual(units,['crash.c','portable_launcher.c','release_report.c'])
            self.assertNotIn('generated/source_data/legacy.inc',files)
            self.assertEqual(tables,[])
            private=source/'generated/source_data/legacy.inc';private.parent.mkdir(parents=True)
            private.write_text('/* synthetic private payload marker */\n')
            with self.assertRaisesRegex(ValueError,'source directory'):setup_sources(psp)
            private.unlink()
            for target in ('../outside.h','generated/source_data/../outside.h',
                           'generated/source_data/..','generated/source_data/nested/file.h'):
                launcher.write_text('#include "'+target+'"\n')
                with self.assertRaisesRegex(ValueError,'source directory'):setup_sources(psp)
            launcher.write_text('#include "generated/source_data/legacy.inc"\n')
            private.symlink_to(source/'missing.inc')
            with self.assertRaisesRegex(ValueError,'source directory'):setup_sources(psp)


class AssetStorageAudit(unittest.TestCase):
    def test_native_setup_accepts_only_selected_zero_initialized_matching_tables(self):
        game = {'motion': ('B', 0x8000000, 64), 'sound': ('B', 0x8000100, 16)}
        setup = {'motion': ('B', 0x9000000, 64)}
        self.assertEqual(check_setup_storage(game, setup, {'motion'}, game), (1, 64))
        with self.assertRaisesRegex(ValueError, 'memory boundary'):
            check_setup_storage(game, setup, set(), game)
        for kind in ('R', 'D', 'T'):
            with self.assertRaisesRegex(ValueError, 'not BSS'):
                check_setup_storage(game, {'motion': (kind, 0, 64)}, {'motion'}, game)
        with self.assertRaisesRegex(ValueError, 'ABI differs'):
            check_setup_storage(game, {'motion': ('B', 0, 128)}, {'motion'}, game)

    def test_accepts_only_zero_initialized_linked_storage(self):
        actual = symbols('data B 08804000 40\nother R 08804100 20\n')
        self.assertEqual(check_storage({'data'}, actual), 64)
        with self.assertRaisesRegex(ValueError, 'not BSS'):
            check_storage({'other'}, actual)
        with self.assertRaisesRegex(ValueError, 'missing'):
            check_storage({'missing'}, actual)

    def test_source_binding_ignores_outputs_but_tracks_source_identity_and_build_flags(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'psp'
            for folder in ('src','tools/meprx','tools/crashprx'):
                (root/folder).mkdir(parents=True)
            (root.parent/'decompile/tools').mkdir(parents=True,exist_ok=True)
            (root.parent/'decompile/tools/crash_build_id.py').write_text('# source fingerprint generator\n')
            (root/'Makefile').write_text('CFLAGS=-O2\n')
            (root/'src/main.c').write_text('int main(void){return 0;}\n')
            identity=root/'src/portable_asset_identity.h'
            identity.write_text('#define ASSET_CRC 123\n')
            before=source_binding(root)
            for name in ('src/main.o','src/main.d','src/crash_build.inc','src/portable_asset_identity.h','EBOOT.PBP','original.jsd'):
                (root/name).write_text('generated output')
            self.assertEqual(source_binding(root),before)
            for name in ('src/main.c','Makefile'):
                path=root/name; old=path.read_text();path.write_text(old+'\n')
                self.assertNotEqual(source_binding(root),before)
                path.write_text(old)
            (root/'tools/meprx/job.c').write_text('int helper;\n')
            self.assertNotEqual(source_binding(root),before)

    def test_source_binding_rejects_links(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'psp'
            for folder in ('src','tools/meprx','tools/crashprx'):
                (root/folder).mkdir(parents=True)
            (root.parent/'decompile/tools').mkdir(parents=True,exist_ok=True)
            (root.parent/'decompile/tools/crash_build_id.py').write_text('# source fingerprint generator\n')
            (root/'Makefile').write_text('all:\n')
            (root/'src/linked.h').symlink_to(root/'Makefile')
            with self.assertRaisesRegex(ValueError,'Linked'):
                source_binding(root)

    def test_pbp_rejects_unreviewed_artwork_and_bad_offsets(self):
        offsets = [40, 44, 44, 44, 44, 44, 44, 96]
        data = b'\0PBP' + b'\0'*4 + struct.pack('<8I', *offsets) + b'SFO!' + b'X'*52 + b'bundle'
        self.assertEqual(pbp_parts(data), (b'X'*52, b'bundle'))
        for at, value in ((12, 43), (8, 39), (36, len(data))):
            bad = bytearray(data);struct.pack_into('<I', bad, at, value)
            with self.assertRaises(ValueError):pbp_parts(bad)


class SplitAudit(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.build = Path(self.temp.name)/'psp'
        self.setup = self.build/'.setup'
        for name in ('src', '.setup', 'tools/meprx', 'tools/crashprx'):
            (self.build/name).mkdir(parents=True)
        names = ('portable_assets.c', 'portable_assets.h', 'portable_asset_identity.h',
                 'source_fmac.h', 'source_float_fast.h', 'stage_texture_usage.inc', 'crash.c', 'release_report.c')
        rows = []
        for name in names:
            (self.build/'src'/name).write_text('/* authored fixture */\n')
            (self.setup/name).write_bytes((self.build/'src'/name).read_bytes())
            rows.append(dict(path='psp/src/'+name, sha256=digest(self.build/'src'/name)))
        (self.setup/'Makefile').write_text('CFLAGS=-O2\n')
        (self.setup/'portable_assets.d').write_text('portable_assets.o: portable_assets.c portable_assets.h\n')
        self.modules = {}
        for index, (elf, prx) in enumerate((('.setup/jgrsetup.elf', '.setup/jgrsetup.prx'),
                ('jgrview.elf', 'jgrview.prx'), ('tools/meprx/jgr_me.elf', 'jgr_me.prx'),
                ('tools/crashprx/jgr_crash.elf', 'jgr_crash.prx'))):
            data = bytearray(56);data[:7] = b'\x7fELF\x01\x01\x01'
            struct.pack_into('<HH', data, 16, 0xffa0, 8);struct.pack_into('<H', data, 40, 52)
            data[-1] = index
            (self.build/prx).write_bytes(data);(self.build/elf).write_bytes(b'authored ELF fixture'+bytes([index]))
            self.modules[str(self.build/elf)] = bytes(data)
        (self.build/'runtime-notices.txt').write_text('Authored test notice\n')
        (self.build/'install.recipe').write_bytes(b'authored test metadata')
        packed, report = bundle.build(self.build/'jgr_me.prx', self.build/'jgr_crash.prx',
            self.build/'runtime-notices.txt', self.build/'install.recipe', self.setup/'jgrsetup.prx')
        setup = (self.build/'jgrview.prx').read_bytes()
        pbp = b'\0PBP'+b'\0'*4+struct.pack('<8I',40,44,44,44,44,44,44,44+len(setup))+b'SFO!'+setup+packed
        (self.build/'EBOOT.PBP').write_bytes(pbp)
        (self.build/'portable_helpers.psar').write_bytes(packed);(self.setup/'runtime.psar').write_bytes(packed)
        receipt=dict(schema=2,architecture='game-main-converter-module',memory_model='PSP-1000',bundle=report,setup_sources=rows,
            eboot_sha256=digest(self.build/'EBOOT.PBP'),setup_elf_sha256=digest(self.setup/'jgrsetup.elf'),
            setup_makefile_sha256=digest(self.setup/'Makefile'))
        (self.setup/'build-receipt.json').write_text(json.dumps(receipt))
        self.setup_symbols='module_start T c 4\nmodule_stop T 10 4\n__library_exports R 14 4\njgr_disc_open T 0 4\njgr_install_run T 4 4\njgr_replay_session_generate T 8 4\n'
        self.actual={'jgr_data_fixture':('B',0,4), '__wrap___libcglue_init':('T',16,4)}

    def tearDown(self):self.temp.cleanup()

    def audit(self, mismatch=False):
        def generate(command, **kwargs):
            Path(command[2]).write_bytes(b'wrong module' if mismatch else self.modules[command[1]])
        with patch('audit_build.subprocess.check_output',return_value=self.setup_symbols), patch('audit_build.subprocess.run',side_effect=generate):
            return audit_split(self.build,'nm','prxgen',{'jgr_data_fixture'},self.actual,{'original_payload.inc'},1)

    def test_binds_both_executables_and_all_bundle_roles(self):
        result=self.audit()
        self.assertEqual(result['bundle_roles'],[1,2,3,4,5])
        self.assertEqual(result['setup_source_files'],8)
        self.assertEqual(result['setup_dependency_files'],1)
        self.assertEqual(result['game_prx_sha256'],digest(self.build/'jgrview.prx'))

    def test_source_copy_or_recipe_changes_fail(self):
        for path in (self.setup/'portable_assets.c',self.build/'install.recipe',self.setup/'Makefile'):
            old=path.read_bytes();path.write_bytes(old+b'changed')
            with self.subTest(path=path.name),self.assertRaises(ValueError):self.audit()
            path.write_bytes(old)

    def test_private_setup_dependency_and_module_substitution_fail(self):
        path=self.setup/'portable_assets.d';old=path.read_text()
        path.write_text(old+'\nfixture.o: original_payload.inc\n')
        with self.assertRaisesRegex(ValueError,'Original data'):self.audit()
        path.write_text(old)
        with self.assertRaisesRegex(ValueError,'does not derive'):self.audit(mismatch=True)

    def test_game_importer_and_setup_asset_storage_fail(self):
        self.actual['jgr_replay_session_generate']=('T',4,4)
        with self.assertRaisesRegex(ValueError,'memory boundary'):self.audit()
        self.actual.pop('jgr_replay_session_generate')
        self.setup_symbols+='jgr_data_fixture B 12 4\n'
        with self.assertRaisesRegex(ValueError,'memory boundary'):self.audit()


if __name__ == '__main__':
    unittest.main()
