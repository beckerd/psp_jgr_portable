"""Browser entry transaction: compatible recipe, verified output, receipt last."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# Synthetic native-v1 recipe header with the current asset layout. These
# are format/count/checksum metadata, not original disc bytes or game assets.
# Test the explicit release identity override: the development tree can carry
# a different profile's identity header and must not silently select it.
NATIVE_HEADER = bytes.fromhex('4a4752494e53310004000000b80100006407000064070000982c2523f423947e0200000074101c00')
NATIVE_IDENTITY = '''#define JGR_ASSET_LAYOUT_VERSION 2u
#define JGR_ASSET_MANIFEST_CRC32 0x7e9423f4u
#define JGR_ASSET_FILE_COUNT 1892u
'''
DRIVER = r'''
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#include "portable_bundle.h"
#include "portable_install.h"
#include "portable_startup_warning.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
unsigned scenario,bundles,installs,verifies,done_events;
unsigned warning_checks,warning_prepares,disc_opens,disc_closes;
int nested_status;
static int cancel_requested;
static int disc_token;
static char recipe[768];
int jgr_browser_prepare_startup_warning(const char *,const char *);
void reset(unsigned value){scenario=value;bundles=installs=verifies=done_events=0;
 warning_checks=warning_prepares=disc_opens=disc_closes=0;nested_status=0;cancel_requested=value==7;}
int jgr_browser_cancelled(void){return cancel_requested;}
void jgr_browser_progress(const char *phase,unsigned current,unsigned total,double complete,double required)
{(void)current;(void)total;(void)complete;(void)required;if(!strcmp(phase,"done"))done_events++;}
int portable_bundle_prepare_progress(const char *root,PortableBundleResult *result,PortableBundleProgress progress,void *user)
{bundles++;memset(result,0,sizeof *result);snprintf(recipe,sizeof recipe,"%s/install.recipe",root);
 progress(1,1,"Synthetic bundle",user);if(scenario==1)result->status=-1;return result->status;}
int portable_bundle_path(unsigned role,char *out,size_t bytes)
{assert(role==PORTABLE_BUNDLE_RECIPE);snprintf(out,bytes,"%s",recipe);return 0;}
const char *portable_bundle_error(int status){(void)status;return "synthetic bundle failure";}
int jgr_install_run(const char *root,const char *plan,const char *gdi,int partial,JgrInstallProgress progress,void *user,JgrInstallResult *result)
{
 assert(root&&plan&&gdi&&!partial);assert(access("data/cache/assets.checked",F_OK));
 installs++;memset(result,0,sizeof *result);result->total=1892;result->phase=JGR_INSTALL_PHASE_CONVERT;
 if(scenario==8)cancel_requested=1;
 if(!progress(result,user))return JGR_INSTALL_CANCELLED;
 if(scenario==2){snprintf(result->detail,sizeof result->detail,"synthetic install failure");return JGR_INSTALL_IO;}
 return 0;
}
const char *jgr_install_error(int status){(void)status;return "Install failed";}
int portable_assets_init(PortableAssetsResult *result,void (*progress)(unsigned,unsigned,const char *))
{
 verifies++;memset(result,0,sizeof *result);
 if(scenario==3)return PORTABLE_ASSETS_CHECKSUM;
 result->files=1892;result->cache_written=scenario!=4;
 if(result->cache_written){FILE *f=fopen("data/cache/assets.checked","wb");assert(f);assert(fputs("verified\n",f)>=0);assert(!fclose(f));}
 if(scenario==9)cancel_requested=1;
 progress(1892,1892,"Synthetic asset");return 0;
}
const char *portable_assets_error(int status){(void)status;return "synthetic verification failure";}
int portable_startup_warning_valid(const char *root)
{
 assert(root&&*root);warning_checks++;
 if(scenario==15)cancel_requested=1;
 if(scenario==16)nested_status=jgr_browser_prepare_startup_warning(root,"/disc/image.gdi");
 return scenario<10 || scenario==15 || scenario==16;
}
int jgr_disc_open(JgrDisc **out,const char *gdi,char *error,size_t bytes)
{
 assert(out&&gdi&&!strcmp(gdi,"/disc/image.gdi"));disc_opens++;*out=NULL;
 if(scenario==11){snprintf(error,bytes,"synthetic invalid disc");return -1;}
 *out=(JgrDisc *)(void *)&disc_token;
 if(scenario==13)cancel_requested=1;
 return 0;
}
void jgr_disc_close(JgrDisc *disc)
{assert(disc==(JgrDisc *)(void *)&disc_token);disc_closes++;}
int portable_startup_warning_prepare(JgrDisc *disc,const char *root)
{
 assert(disc==(JgrDisc *)(void *)&disc_token&&root&&*root);
 /* Core conversion and explicit repair both retain a completion receipt. */
 assert(!access("data/cache/assets.checked",F_OK));warning_prepares++;
 if(scenario==14)cancel_requested=1;
 return scenario==12?-1:0;
}
'''


class BrowserEntry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-browser-entry-build-')
        folder = Path(cls.build.name)
        (folder / 'driver.c').write_text(DRIVER)
        (folder / 'release_identity.h').write_text(NATIVE_IDENTITY)
        subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-DJGR_PORTABLE_ASSETS=1',
                        '-DJGR_PORTABLE_IDENTITY_HEADER="release_identity.h"',
                        '-I' + str(folder), '-I' + str(ROOT / 'psp/src'), str(folder / 'driver.c'),
                        str(ROOT / 'tools/portable/browser/prepare.c'),
                        '-o', str(folder / 'entry.so')], check=True)
        cls.lib = C.CDLL(str(folder / 'entry.so'))
        cls.lib.jgr_browser_prepare.argtypes = [C.c_char_p, C.c_char_p]
        cls.lib.jgr_browser_prepare_startup_warning.argtypes = [C.c_char_p, C.c_char_p]
        cls.lib.jgr_browser_error.restype = C.c_char_p

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix='jgr-browser-entry-')
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name) / 'PSP/GAME/JGR_PORTABLE'
        (self.root / 'data/cache').mkdir(parents=True)
        self.seal = self.root / 'data/cache/assets.checked'
        self.seal.write_bytes(b'old receipt\n')
        self.save = Path(self.work.name) / 'PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav'
        self.save.parent.mkdir(parents=True)
        self.save.write_bytes(b'existing player save\0\xff')
        (self.root / 'data/assets').mkdir()
        self.asset = self.root / 'data/assets/core-fixture.bin'
        self.asset.write_bytes(b'verified core fixture\0\xff')
        self.protected = [(p, p.read_bytes(), p.stat().st_mtime_ns) for p in (self.save, self.asset)]
        self.recipe = NATIVE_HEADER
        (self.root / 'install.recipe').write_bytes(self.recipe)
        self.lib.reset(0)

    def count(self, name):
        return C.c_uint.in_dll(self.lib, name).value

    def run_entry(self, scenario=0):
        before = os.getcwd()
        self.lib.reset(scenario)
        status = self.lib.jgr_browser_prepare(os.fsencode(self.root), b'/disc/image.gdi')
        self.assertEqual(os.getcwd(), before)
        self.assert_protected()
        return status

    def assert_protected(self):
        for path, data, timestamp in self.protected:
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(path.stat().st_mtime_ns, timestamp)

    def run_warning(self, scenario=0):
        before = os.getcwd()
        seal = (self.seal.read_bytes(), self.seal.stat().st_mtime_ns)
        self.lib.reset(scenario)
        status = self.lib.jgr_browser_prepare_startup_warning(os.fsencode(self.root), b'/disc/image.gdi')
        self.assertEqual(os.getcwd(), before)
        self.assertEqual((self.seal.read_bytes(), self.seal.stat().st_mtime_ns), seal)
        self.assertEqual(self.count('installs'), 0)
        self.assertEqual(self.count('verifies'), 0)
        self.assertEqual(self.count('done_events'), 0)
        self.assert_protected()
        return status

    def test_success_requires_verification_and_publishes_receipt(self):
        self.assertEqual(self.run_entry(), 0)
        self.assertEqual([self.count(x) for x in ('bundles', 'installs', 'verifies', 'done_events')], [1, 1, 1, 1])
        self.assertEqual(self.seal.read_bytes(), b'verified\n')
        self.assertEqual(self.lib.jgr_browser_asset_count(), 1892)
        self.assertEqual(self.lib.jgr_browser_error(), b'')

    def test_mismatched_recipe_never_starts_or_invalidates_completed_assets(self):
        for offset in (8, 12, 16, 28, 32):
            with self.subTest(offset=offset):
                bad = bytearray(self.recipe)
                bad[offset] ^= 1
                (self.root / 'install.recipe').write_bytes(bad)
                self.assertEqual(self.run_entry(), -102)
                self.assertEqual(self.count('installs'), 0)
                self.assertEqual(self.seal.read_bytes(), b'old receipt\n')

    def test_original_profile_is_rejected_by_native_release_identity(self):
        old = bytearray(self.recipe)
        struct.pack_into('<I', old, 28, 0x9918ac54)
        (self.root / 'install.recipe').write_bytes(old)
        self.assertEqual(self.run_entry(), -102)
        self.assertEqual(self.count('installs'), 0)
        self.assertEqual(self.seal.read_bytes(), b'old receipt\n')

    def test_bad_bundle_preserves_previous_receipt(self):
        self.assertEqual(self.run_entry(1), -101)
        self.assertEqual(self.count('installs'), 0)
        self.assertTrue(self.seal.exists())

    def test_install_verify_and_receipt_failures_never_leave_completion_marker(self):
        for scenario in (2, 3, 4):
            with self.subTest(scenario=scenario):
                self.seal.write_bytes(b'old receipt\n')
                self.assertNotEqual(self.run_entry(scenario), 0)
                self.assertFalse(self.seal.exists())
                self.assertEqual(self.count('done_events'), 0)
                self.assertEqual(self.lib.jgr_browser_asset_count(), 0)

    def test_cancellation_before_mutation_preserves_existing_receipt(self):
        self.assertEqual(self.run_entry(7), -7)
        self.assertEqual(self.count('bundles'), 0)
        self.assertTrue(self.seal.exists())

    def test_cancellation_during_conversion_or_verification_removes_receipt(self):
        for scenario in (8, 9):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.run_entry(scenario), -7)
                self.assertFalse(self.seal.exists())
                self.assertEqual(self.count('done_events'), 0)

    def test_relative_paths_are_rejected_before_any_work(self):
        self.assertEqual(self.lib.jgr_browser_prepare(b'relative', b'/disc/image.gdi'), -100)
        self.assertEqual(self.count('bundles'), 0)
        self.assertTrue(self.seal.exists())

    def test_valid_warning_reused_without_opening_disc(self):
        self.assertEqual(self.run_entry(), 0)
        self.assertEqual([self.count(x) for x in ('warning_checks', 'disc_opens', 'warning_prepares', 'disc_closes')], [1, 0, 0, 0])
        self.assertEqual(self.run_warning(), 0)
        self.assertEqual([self.count(x) for x in ('warning_checks', 'disc_opens', 'warning_prepares', 'disc_closes')], [1, 0, 0, 0])

    def test_missing_warning_generated_after_core_verification(self):
        self.assertEqual(self.run_entry(10), 0)
        self.assertEqual(self.seal.read_bytes(), b'verified\n')
        self.assertEqual([self.count(x) for x in ('verifies', 'disc_opens', 'warning_prepares', 'disc_closes', 'done_events')], [1, 1, 1, 1, 1])
        self.assertEqual(self.lib.jgr_browser_asset_count(), 1892)

    def test_optional_warning_failures_keep_verified_core_receipt(self):
        for scenario in (11, 12):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.run_entry(scenario), 0)
                self.assertEqual(self.seal.read_bytes(), b'verified\n')
                self.assertEqual(self.count('done_events'), 1)
                self.assertEqual(self.lib.jgr_browser_asset_count(), 1892)
                self.assertEqual(self.lib.jgr_browser_error(), b'')
                self.assertEqual(self.count('disc_closes'), int(scenario == 12))

    def test_optional_warning_cancellation_preserves_committed_receipt(self):
        for scenario, opened, prepared in ((13, 1, 0), (14, 1, 1), (15, 0, 0)):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.run_entry(scenario), -7)
                self.assertEqual(self.seal.read_bytes(), b'verified\n')
                self.assertEqual(self.count('done_events'), 0)
                self.assertEqual(self.lib.jgr_browser_asset_count(), 1892)
                self.assertEqual([self.count(x) for x in ('disc_opens', 'warning_prepares', 'disc_closes')], [opened, prepared, opened])

    def test_supplemental_warning_success_failure_and_cancellation_leave_core_untouched(self):
        for scenario, expected in ((10, 0), (11, -107), (12, -107), (7, -7), (13, -7), (14, -7), (15, -7)):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.run_warning(scenario), expected)
                self.assertEqual(self.count('disc_closes'), int(scenario in (10, 12, 13, 14)))

    def test_supplemental_warning_still_binds_eboot_recipe_before_reuse(self):
        self.assertEqual(self.run_warning(1), -101)
        self.assertEqual(self.count('warning_checks'), 0)
        for offset in (8, 12, 16, 28, 32):
            with self.subTest(offset=offset):
                bad = bytearray(self.recipe)
                bad[offset] ^= 1
                (self.root / 'install.recipe').write_bytes(bad)
                self.assertEqual(self.run_warning(), -102)
                self.assertEqual(self.count('warning_checks'), 0)
                self.assertEqual(self.count('disc_opens'), 0)

    def test_supplemental_warning_reentrant_call_is_rejected(self):
        self.assertEqual(self.run_entry(16), 0)
        self.assertEqual(C.c_int.in_dll(self.lib, 'nested_status').value, -100)
        self.assertEqual(self.count('bundles'), 1)
        self.assertEqual(self.count('warning_checks'), 1)

    def test_supplemental_warning_rejects_bad_paths_before_work(self):
        for root, gdi in ((b'relative', b'/disc/image.gdi'), (os.fsencode(self.root), b'relative')):
            self.assertEqual(self.lib.jgr_browser_prepare_startup_warning(root, gdi), -100)
        self.assertEqual(self.count('bundles'), 0)
        self.assertEqual(self.seal.read_bytes(), b'old receipt\n')
        self.assert_protected()


if __name__ == '__main__':
    unittest.main()
