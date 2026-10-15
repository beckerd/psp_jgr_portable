"""Production portable IO/bootstrap with disposable assets, never game data."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
DATA = {'font.bin': b'F' * 8193, 'grf/s00.bin': b'G' * 177, 'music1.adx': b'M' * 513}
SILENT_COUNTERS = ('asset_stats', 'asset_opens', 'manifest_opens', 'background_opens',
                   'file_writes', 'directory_writes', 'debug_calls', 'background_alloc_calls')


def manifest(data=DATA):
    rows = ['JGR_ASSETS 1', 'layout 2', f'count {len(data)}']
    rows += [f'{len(value)} {zlib.crc32(value):08x} {name}' for name, value in sorted(data.items())]
    return ('\n'.join(rows) + '\n').encode('ascii')


class Result(C.Structure):
    _fields_ = [('status', C.c_int), ('files', C.c_uint),
                ('full_verification', C.c_uint), ('cache_written', C.c_uint),
                ('file', C.c_char * 192)]


class PortableAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='jgr-portable-build-')
        build = Path(cls.build.name)
        identity = build / 'identity.h'
        identity.write_text(f'#define JGR_ASSET_LAYOUT_VERSION 2u\n'
                            f'#define JGR_ASSET_MANIFEST_CRC32 0x{zlib.crc32(manifest()):08x}u\n'
                            f'#define JGR_ASSET_FILE_COUNT {len(DATA)}u\n'
                            '#include <stddef.h>\n'
                            'int portable_test_raw_stat(const char*,struct stat*);\n'
                            'FILE *portable_test_raw_fopen(const char*,const char*);\n'
                            'int portable_test_raw_remove(const char*);\n'
                            'int portable_test_raw_mkdir(const char*,mode_t);\n'
                            'int portable_test_raw_rename(const char*,const char*);\n'
                            'char *portable_test_raw_getcwd(char*,size_t);\n'
                            '#define getcwd portable_test_raw_getcwd\n'
                            '#define stat(name,info) portable_test_raw_stat(name,info)\n'
                            '#define fopen(name,mode) portable_test_raw_fopen(name,mode)\n'
                            '#define remove(name) portable_test_raw_remove(name)\n'
                            '#define mkdir(name,mode) portable_test_raw_mkdir(name,mode)\n'
                            '#define rename(from,to) portable_test_raw_rename(from,to)\n'
                            'extern unsigned portable_test_screen[];\n'
                            '#define JGR_PORTABLE_SCREEN_VRAM (portable_test_screen+512)\n'
                            'void *portable_test_background_alloc(size_t);\n'
                            'void portable_test_background_free(void *);\n'
                            '#define JGR_PORTABLE_SCREEN_ALLOC portable_test_background_alloc\n'
                            '#define JGR_PORTABLE_SCREEN_FREE portable_test_background_free\n')
        library = build / 'portable.so'
        implementation = build / 'assets_test.c'
        implementation.write_text('#include "portable_assets.c"\n'
                                  'int portable_test_finish_install(PortableAssetsResult *r)\n'
                                  '{return assets_init(r,NULL,1);}\n'
                                  'void portable_test_reset_exit(void){setup_exit_requested=0;}\n')
        savedata = build / 'savedata_test.c'
        savedata.write_text('#include "savedata.c"\n'
                            'void savedata_test_reset_service(void){save_hash_service=0;}\n')
        command = ['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-local-typedef',
                   '-DJGR_PORTABLE_ASSETS=1', '-D__PSP__=1', f'-DJGR_PORTABLE_IDENTITY_HEADER="{identity}"',
                   '-include', 'psp/src/portable_assets.h', '-Ipsp/src',
                   '-Ipsp/tests/portable_assets_stubs', '-Ipsp/tests/stubs',
                   str(implementation), 'psp/tests/portable_assets_bridge.c',
                   'psp/tests/portable_assets_psp_bridge.c',
                   'psp/src/story.c', str(savedata), 'psp/tests/savedata_hash_bridge.c',
                   '-lm', '-o', str(library)]
        subprocess.run(command, cwd=ROOT, check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.portable_assets_init.argtypes = [C.POINTER(Result), C.c_void_p]
        cls.lib.portable_test_finish_install.argtypes = [C.POINTER(Result)]
        cls.lib.portable_assets_chdir_application.argtypes = [C.c_char_p]
        cls.lib.portable_assets_resolve.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
        cls.lib.savedata_update.argtypes = [C.c_char_p, C.c_char_p, C.c_int]
        cls.hash_step = C.c_int.in_dll(cls.lib, 'savedata_test_hash_step')
        cls.hash_fail = C.c_int.in_dll(cls.lib, 'savedata_test_hash_fail')
        cls.hash_bad = C.c_int.in_dll(cls.lib, 'savedata_test_hash_bad')
        cls.hash_input = ((C.c_ubyte * 5120) * 2).in_dll(cls.lib, 'savedata_test_hash_input')
        cls.hash_bytes = (C.c_uint * 2).in_dll(cls.lib, 'savedata_test_hash_bytes')
        cls.hash_noop = C.c_int.in_dll(cls.lib, 'savedata_test_hash_noop')
        cls.service_fail = C.c_int.in_dll(cls.lib, 'savedata_test_service_fail')
        cls.service_loaded = C.c_int.in_dll(cls.lib, 'savedata_test_service_loaded')
        cls.service_started = C.c_int.in_dll(cls.lib, 'savedata_test_service_started')
        cls.service_calls = C.c_int.in_dll(cls.lib, 'savedata_test_service_calls')
        cls.lib.portable_test_read.argtypes = cls.lib.portable_test_exists.argtypes = [C.c_char_p]
        cls.lib.portable_test_remove.argtypes = [C.c_char_p]
        cls.lib.portable_test_write.argtypes = cls.lib.portable_test_rename.argtypes = [C.c_char_p, C.c_char_p]
        cls.lib.story_save.argtypes = cls.lib.story_load.argtypes = [C.c_char_p]
        cls.lib.portable_test_sce_read.argtypes = [C.c_char_p]
        cls.lib.portable_test_sce_write_open.argtypes = [C.c_char_p, C.c_int]
        cls.lib.portable_test_sce_stat.argtypes = cls.lib.portable_test_sce_remove.argtypes = [C.c_char_p]
        cls.lib.portable_test_sce_rename.argtypes = [C.c_char_p, C.c_char_p]
        cls.story = (C.c_ubyte * 1024).in_dll(cls.lib, 'story')
        cls.app_root = (C.c_char * 512).in_dll(cls.lib, 'portable_test_app_root')
        cls.screen = (C.c_uint32 * (512 * 274)).in_dll(cls.lib, 'portable_test_screen')
        cls.background_live=C.c_int.in_dll(cls.lib,'portable_test_background_live')
        cls.background_fail_alloc=C.c_int.in_dll(cls.lib,'portable_test_background_fail_alloc')
        cls.background_size=C.c_uint.in_dll(cls.lib,'portable_test_background_last_size')
        cls.power_ticks=C.c_uint.in_dll(cls.lib,'portable_test_power_ticks')
        cls.asset_stats=C.c_uint.in_dll(cls.lib,'portable_test_asset_stats')
        cls.asset_opens=C.c_uint.in_dll(cls.lib,'portable_test_asset_opens')
        cls.seal_remove_fail=C.c_uint.in_dll(cls.lib,'portable_test_seal_remove_fail')
        font = (C.c_ubyte * (256 * 8)).in_dll(cls.lib, 'msx')
        font[:] = [0x7e] * len(font)
        font[32 * 8:33 * 8] = [0] * 8

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='jgr-portable-case-')
        self.old = Path.cwd()
        self.background_fail_alloc.value=0
        self.background_size.value=0
        self.asset_stats.value=self.asset_opens.value=0
        self.seal_remove_fail.value=0
        self.app_root.value=b''
        self.hash_step.value = self.hash_fail.value = self.hash_bad.value = 0
        self.hash_noop.value = self.service_fail.value = self.service_calls.value = 0
        self.service_loaded.value = self.service_started.value = 0
        self.lib.savedata_test_reset_service()
        self.lib.portable_test_reset_exit()
        self.assertEqual(self.background_live.value,0)
        os.chdir(self.directory.name)
        for name, data in DATA.items():
            path = Path('data/assets') / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        Path('data/assets/assets-manifest.txt').write_bytes(manifest())

    def tearDown(self):
        self.assertEqual(self.background_live.value,0,'Setup background remains allocated after returning')
        os.chdir(self.old)
        self.directory.cleanup()

    def init(self):
        result = Result()
        status = self.lib.portable_assets_init(C.byref(result), None)
        self.assertEqual(status, result.status)
        return result

    def reset_silent_counters(self, lib=None):
        for name in SILENT_COUNTERS:
            C.c_uint.in_dll(lib or self.lib, 'portable_test_'+name).value=0

    def assert_silent_counters(self, lib=None):
        for name in SILENT_COUNTERS:
            self.assertEqual(C.c_uint.in_dll(lib or self.lib, 'portable_test_'+name).value,0,name)

    def files_snapshot(self):
        return {str(p):(p.read_bytes(),p.stat().st_mtime_ns)
                for p in Path('data').rglob('*') if p.is_file()}

    def test_first_boot_full_crc_then_cached_boot_without_asset_rewrite(self):
        before = {p: p.stat().st_mtime_ns for p in Path('data/assets').rglob('*') if p.is_file()}
        result = self.init()
        self.assertEqual((result.status, result.files, result.full_verification, result.cache_written), (0, 3, 1, 1))
        self.assertTrue(Path('data/cache/assets.checked').is_file())
        self.assertFalse(Path('data/cache/assets.checked.tmp').exists())
        self.assertEqual((self.asset_stats.value,self.asset_opens.value),(6,3))
        self.asset_stats.value=self.asset_opens.value=0
        result = self.init()
        self.assertEqual((result.status, result.full_verification, result.cache_written), (0, 0, 0))
        self.assertEqual((self.asset_stats.value,self.asset_opens.value),(0,0))
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})

    def test_bootstrap_trusts_completed_install_without_payload_io(self):
        self.assertEqual(self.init().status,0)
        self.write_background(self.background())
        Path('data/log/setup.log').write_bytes(b'previous install log\n')
        seal=Path('data/cache/assets.checked').read_bytes()
        self.assertTrue(seal.startswith(b'JGR_CHECKED 1\n'))
        snapshot=self.files_snapshot()
        self.screen[:]=[0x12345678]*len(self.screen)
        self.reset_silent_counters()
        for _ in range(2):
            self.assertEqual(self.lib.portable_assets_bootstrap(),0)
            self.lib.portable_assets_setup_tick()
        self.assert_silent_counters()
        self.assertTrue(all(p==0x12345678 for p in self.screen))
        self.assertEqual(self.files_snapshot(),snapshot)
        self.assertEqual(Path('data/cache/assets.checked').read_bytes(),seal)

    def test_direct_bootstrap_trusts_marker_without_reading_manifest(self):
        self.assertEqual(self.init().status,0)
        Path('data/assets/assets-manifest.txt').unlink()
        self.reset_silent_counters()
        self.assertEqual(self.lib.portable_assets_bootstrap(),0)
        self.assert_silent_counters()
        self.assertEqual(self.lib.portable_test_read(b'font.bin'),ord('F'))
        self.assertEqual(self.init().status,-1,'Explicit validation must still check the manifest')

    def test_cancelled_direct_bootstrap_does_not_draw_or_reenable_asset_reads(self):
        self.assertEqual(self.init().status,0)
        self.screen[:]=[0x12345678]*len(self.screen)
        self.reset_silent_counters()
        self.lib.portable_assets_setup_request_exit()
        self.assertEqual(self.lib.portable_assets_bootstrap(),-10)
        self.assert_silent_counters()
        self.assertTrue(all(p==0x12345678 for p in self.screen))
        self.assertEqual(self.lib.portable_test_read(b'font.bin'),-1)

    def test_invalid_marker_falls_back_to_visible_verification(self):
        self.assertEqual(self.init().status,0)
        Path('data/cache/assets.checked').write_bytes(b'partial')
        self.screen[:]=[0x12345678]*len(self.screen)
        self.reset_silent_counters()
        self.assertEqual(self.lib.portable_assets_bootstrap(),0)
        self.assertGreater(self.asset_stats.value,0)
        self.assertGreater(self.asset_opens.value,0)
        self.assertGreater(C.c_uint.in_dll(self.lib,'portable_test_manifest_opens').value,0)
        self.assertTrue(any(p!=0x12345678 for p in self.screen))
        self.assert_screen_bounds()

    def test_valid_seal_does_not_override_missing_or_changed_manifest(self):
        self.assertEqual(self.init().status,0)
        index=Path('data/assets/assets-manifest.txt')
        index.write_bytes(manifest().replace(b'font.bin',b'evil.bin'))
        self.assertEqual(self.init().status,-4)
        index.unlink()
        self.assertEqual(self.init().status,-1)

    def test_completed_install_seals_metadata_without_second_content_check(self):
        result = Result()
        self.assertEqual(self.lib.portable_test_finish_install(C.byref(result)), 0)
        self.assertEqual((result.files, result.full_verification, result.cache_written), (3, 0, 1))
        self.assertTrue(Path('data/cache/assets.checked').is_file())
        self.assertEqual((self.init().status, self.init().full_verification), (0, 0))

    def test_completion_cannot_republish_when_old_seal_cannot_be_invalidated(self):
        self.assertEqual(self.init().status,0)
        seal=Path('data/cache/assets.checked');old=seal.read_bytes()
        self.seal_remove_fail.value=1
        self.asset_stats.value=self.asset_opens.value=0
        result=Result()
        self.assertEqual(self.lib.portable_test_finish_install(C.byref(result)),-8)
        self.assertEqual((self.asset_stats.value,self.asset_opens.value),(0,0))
        self.assertEqual(seal.read_bytes(),old)
        self.assertEqual(self.lib.portable_test_read(b'font.bin'),-1)
        self.lib.portable_assets_data_error()
        self.assertEqual(seal.read_bytes(),old)
        self.assertEqual(self.lib.portable_test_read(b'font.bin'),-1)

    def test_completed_install_still_rejects_bad_manifest_missing_and_resized_files(self):
        index = Path('data/assets/assets-manifest.txt')
        asset = Path('data/assets/music1.adx')
        for change in ('manifest', 'missing', 'size'):
            with self.subTest(change=change):
                index.write_bytes(manifest())
                asset.write_bytes(DATA['music1.adx'])
                self.assertEqual(self.init().status,0)
                if change == 'manifest':
                    index.write_bytes(manifest().replace(b'font.bin', b'evil.bin'))
                elif change == 'missing':
                    asset.unlink()
                else:
                    asset.write_bytes(DATA['music1.adx'][:-1])
                result = Result()
                self.assertLess(self.lib.portable_test_finish_install(C.byref(result)), 0)
                self.assertFalse(Path('data/cache/assets.checked').exists())

    def test_verification_keeps_power_awake_and_handoff_restores_absolute_root(self):
        app=Path.cwd()
        child=app/'nested'
        child.mkdir()
        os.chdir(child)
        self.assertLess(self.lib.portable_assets_chdir_application(b'..'),0)
        self.assertEqual(Path.cwd(),child)
        self.assertEqual(self.lib.portable_assets_chdir_application(str(app).encode()),0)
        before=self.power_ticks.value
        self.assertEqual(self.init().status,0)
        self.assertGreater(self.power_ticks.value,before)

    def test_device_setup_failure_preserves_converter_detail_in_log(self):
        bridge=Path(self.build.name)/'device_failure.c'
        bridge.write_text('''#include "portable_assets.c"
#include "portable_install.h"
#include "portable_models_jobs.h"
void jgr_models_stats(JgrModelsStats *out){memset(out,0,sizeof *out);out->jobs=7;out->geometry_us=1234;}
void jgr_stage_stats(JgrStageStats *s){memset(s,0,sizeof *s);s->jobs=2;s->completed=1;s->geometry_us=2345;}
void jgr_replay_stats(JgrReplayStats *s){memset(s,0,sizeof *s);s->jobs=3;s->outputs=4;s->reused_outputs=1;s->generation_us=3456;strcpy(s->output,"demo.bin");}
void jgr_install_work_stats(JgrInstallWorkStats *s){memset(s,0,sizeof *s);s->city.verify_us=4567;s->replay.publish_us=5678;}
#include "portable_bundle.h"
#include <string.h>
extern unsigned portable_test_screen[];
extern unsigned char msx[];
int heartbeat_inside,heartbeat_outside,screen_checks,screen_failures;
static void test_font(void)
{memset(msx,0,256*8);for(unsigned ch=1;ch<256;ch++)if(ch!=32)msx[ch*8]=(unsigned char)ch;}
static void expect_line(unsigned y,const char *text)
{unsigned pixels[480];for(unsigned x=0;x<480;x++)pixels[x]=0xff101010u;
 unsigned left=(480-8*(unsigned)strlen(text))/2;
 for(unsigned i=0;text[i];i++)for(unsigned bit=0;bit<8;bit++)
   if(text[i]!=32 && ((unsigned char)text[i]&(0x80u>>bit)))pixels[left+i*8+bit]=0xffffffffu;
 screen_checks++;
 if(memcmp(pixels,portable_test_screen+(y+1)*512,sizeof pixels))screen_failures++;}
int portable_bundle_prepare(const char *root,PortableBundleResult *r)
{(void)root;memset(r,0,sizeof *r);return 0;}
int portable_bundle_try_cached(const char *root,PortableBundleResult *r)
{(void)root;memset(r,0,sizeof *r);return PORTABLE_BUNDLE_MISSING;}
int portable_bundle_prepare_progress(const char *root,PortableBundleResult *r,PortableBundleProgress p,void *u)
{static unsigned before[512*274];test_font();if(p)p(1024,2048,"Checking bundled game.prx",u);
 expect_line(136,"");expect_line(160,"Checking application");
 memcpy(before,portable_test_screen,sizeof before);memset(msx,255,256*8);portable_assets_setup_tick();
 for(unsigned i=0;i<512*274;i++)if(before[i]!=portable_test_screen[i]){
   unsigned y=i/512,x=i%512;if(y>=233 && y<246 && x<480)heartbeat_inside++;else heartbeat_outside++;}
 test_font();if(p)p(1024,2048,"Extracting game.prx",u);
 expect_line(136,"");expect_line(160,"Preparing application");
 return portable_bundle_prepare(root,r);}
int portable_bundle_path(unsigned role,char *p,size_t size)
{(void)role;if(size<7)return -1;strcpy(p,"unused");return 0;}
const char *portable_bundle_error(int status){(void)status;return "bundle";}
const char *jgr_install_error(int status){(void)status;return "setup failed";}
int jgr_install_run(const char *root,const char *plan,const char *disc,int partial,
 JgrInstallProgress progress,void *user,JgrInstallResult *r)
{(void)root;(void)plan;(void)disc;(void)partial;memset(r,0,sizeof *r);
 r->phase=JGR_INSTALL_PHASE_SCAN;r->phase_current=31;r->phase_total=1892;
 strcpy(r->file,"grf/s00.bin");
 if(progress)progress(r,user);portable_assets_setup_tick();
 expect_line(136,"Graphics and level data");expect_line(176,"31 / 1892 files checked");
 r->phase=JGR_INSTALL_PHASE_CONVERT;r->phase_current=51;r->completed_bytes=122022606u;
 strcpy(r->file,"adx/DRA_SLA.ADX");if(progress)progress(r,user);
 expect_line(136,"Music and voices");expect_line(160,"Preparing game assets");
 expect_line(176,"51 / 1892 files prepared");expect_line(216,"122 MB prepared | 2% of files");
 strcpy(r->file,"staffroute07_pose.bin");if(progress)progress(r,user);
 expect_line(136,"Demonstrations and credits");
 r->status=JGR_INSTALL_IO;strcpy(r->file,"fixture.bin");
 strcpy(r->detail,"fixture allocation failed: 18874368 bytes");
 if(progress)progress(r,user);expect_line(136,"Graphics and level data");return r->status;}
void portable_test_error_screen(void)
{expect_line(136,"Details saved in the setup log.");
 expect_line(160,"Check your Memory Stick and restart.");
 expect_line(176,"If setup fails again, share the setup log.");}
''')
        library=Path(self.build.name)/'device_failure.so'
        command=['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
                 '-DJGR_PORTABLE_ASSETS=1','-DJGR_DEVICE_SETUP=1','-D__PSP__=1',
                 f'-DJGR_PORTABLE_IDENTITY_HEADER="{Path(self.build.name)/"identity.h"}"',
                 '-include','psp/src/portable_assets.h','-Ipsp/src',
                 '-Ipsp/tests/portable_assets_stubs','-Ipsp/tests/stubs',
                 'psp/tests/portable_assets_psp_bridge.c',
                 str(bridge),'-o',str(library)]
        subprocess.run(command,cwd=ROOT,check=True)
        device=C.CDLL(str(library))
        Path('data/assets/assets-manifest.txt').unlink()
        self.assertLess(device.portable_assets_bootstrap(),0)
        log=Path('data/log/setup.log').read_text()
        self.assertIn('SETUP_MODELS',log)
        self.assertIn('jobs=7',log)
        self.assertIn('geometry_us=1234',log)
        self.assertIn('SETUP_CITY',log)
        self.assertIn('jobs=2 completed=1',log)
        self.assertIn('geometry_us=2345',log)
        self.assertIn('SETUP_REPLAY',log)
        self.assertIn('outputs=4 reused_outputs=1 jobs=3',log)
        self.assertIn('generation_us=3456',log)
        self.assertIn('file=demo.bin',log)
        self.assertIn('SETUP_WORK',log)
        self.assertIn('verify_us=4567',log)
        self.assertIn('publish_us=5678',log)
        self.assertIn('file=fixture.bin',log)
        self.assertIn('detail=fixture allocation failed: 18874368 bytes',log)
        self.assertIn('phase=Checking application current=1 total=2 file=Checking bundled game.prx',log)
        self.assertIn('phase=Preparing application current=1 total=2 file=Extracting game.prx',log)
        self.assertIn('phase=Checking existing files current=31 total=1892',log)
        self.assertIn('phase=Preparing game assets',log)
        self.assertGreater(C.c_int.in_dll(device,'heartbeat_inside').value,0)
        self.assertEqual(C.c_int.in_dll(device,'heartbeat_outside').value,0)
        device.portable_test_error_screen()
        self.assertEqual(C.c_int.in_dll(device,'screen_checks').value,15)
        self.assertEqual(C.c_int.in_dll(device,'screen_failures').value,0)
        screen=(C.c_uint*(512*274)).in_dll(device,'portable_test_screen')
        before=list(screen)
        device.portable_assets_setup_tick()
        self.assertEqual(list(screen),before,'Heartbeat must stop before error/handoff releases background')

    def test_setup_worker_lifecycle_cancel_and_cpu_fallback(self):
        bridge=Path(self.build.name)/'setup_lifecycle.c'
        bridge.write_text(r'''#include "portable_assets.c"
int scenario,verified,starts,installs,stops,notes,ordered,progress_result,crash_starts,crash_ordered,seal_at_install,cached_checks,full_prepares;
void lifecycle_reset(int value)
{scenario=value;verified=starts=installs=stops=notes=ordered=progress_result=crash_starts=crash_ordered=cached_checks=full_prepares=0;setup_exit_requested=0;seal_at_install=-1;}
int crash_init(void){crash_starts++;crash_ordered=verified&&!starts&&!installs;return scenario==3?-1:0;}
const char *crash_build_id(void){return "fixture";}
int portable_bundle_try_cached(const char *root,PortableBundleResult *r)
{(void)root;memset(r,0,sizeof *r);cached_checks++;
 if(scenario==8)portable_assets_setup_request_exit();
 return scenario==6 || scenario==8?0:PORTABLE_BUNDLE_MISSING;}
int portable_bundle_prepare(const char *root,PortableBundleResult *r)
{(void)root;memset(r,0,sizeof *r);full_prepares++;verified=1;
 if(scenario==9)portable_assets_setup_request_exit();return scenario==10?PORTABLE_BUNDLE_IO:0;}
int portable_bundle_prepare_progress(const char *root,PortableBundleResult *r,PortableBundleProgress p,void *u)
{(void)root;(void)p;(void)u;memset(r,0,sizeof *r);if(scenario==1)return -1;
 verified=1;if(scenario==2)portable_assets_setup_request_exit();return 0;}
int portable_bundle_path(unsigned role,char *p,size_t n)
{(void)role;if(n<7)return -1;strcpy(p,"helper");return 0;}
const char *portable_bundle_error(int s){(void)s;return "bundle";}
int jgr_setup_me_init(const char *p){(void)p;starts++;ordered=verified;return scenario!=3;}
int jgr_setup_me_shutdown(void){stops++;return scenario!=5;}
void jgr_setup_me_note_exit(void){notes++;}
void jgr_setup_me_stats(JgrSetupMeStats *s){memset(s,0,sizeof *s);s->jobs=7;s->completed=7;s->cache_us=31;s->wait_us=41;}
void jgr_models_stats(JgrModelsStats *s){memset(s,0,sizeof *s);}
void jgr_stage_stats(JgrStageStats *s){memset(s,0,sizeof *s);}
void jgr_replay_stats(JgrReplayStats *s){memset(s,0,sizeof *s);}
void jgr_install_work_stats(JgrInstallWorkStats *s){memset(s,0,sizeof *s);}
const char *jgr_install_error(int s){(void)s;return "fixture failure";}
int jgr_install_run(const char *root,const char *plan,const char *disc,int partial,
 JgrInstallProgress progress,void *user,JgrInstallResult *r)
{(void)root;(void)plan;(void)disc;(void)partial;installs++;memset(r,0,sizeof *r);
 FILE *seal=fopen(SEAL_NAME,"rb");seal_at_install=seal!=NULL;if(seal)fclose(seal);
 r->phase=JGR_INSTALL_PHASE_CONVERT;r->pipeline.queued=4;r->pipeline.published=3;r->pipeline.overlapped_jobs=2;r->pipeline.drain_us=123;
 if(scenario==4)portable_assets_setup_request_exit();progress_result=progress(r,user);
 return progress_result?JGR_INSTALL_IO:JGR_INSTALL_CANCELLED;}
''')
        library=Path(self.build.name)/'setup_lifecycle.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
            '-DJGR_PORTABLE_ASSETS=1','-DJGR_DEVICE_SETUP=1','-DJGR_SETUP_ME=1','-DJGR_SETUP_CRASH=1','-D__PSP__=1',
            f'-DJGR_PORTABLE_IDENTITY_HEADER="{Path(self.build.name)/"identity.h"}"',
            '-include','psp/src/portable_assets.h','-Ipsp/src','-Ipsp/tests/portable_assets_stubs',
            '-Ipsp/tests/stubs','psp/tests/portable_assets_psp_bridge.c',str(bridge),'-o',str(library)],cwd=ROOT,check=True)
        lib=C.CDLL(str(library))
        get=lambda key:C.c_int.in_dll(lib,key).value
        # A valid installed pack checks the bundle but never starts a worker.
        lib.lifecycle_reset(0)
        self.assertEqual(lib.portable_assets_bootstrap(),0)
        self.assertEqual((get('verified'),get('starts'),get('installs')),(1,0,0))
        self.assertEqual((get('crash_starts'),get('crash_ordered')),(1,1))
        seal=Path('data/cache/assets.checked');completed=seal.read_bytes()
        screen=(C.c_uint32*(512*274)).in_dll(lib,'portable_test_screen')
        for scenario in (6,7,8,9,10):
            with self.subTest(direct_scenario=scenario):
                seal.write_bytes(completed)
                snapshot=self.files_snapshot()
                lib.lifecycle_reset(scenario)
                screen[:]=[0x12345678]*len(screen)
                self.reset_silent_counters(lib)
                self.assertEqual(lib.portable_assets_bootstrap(),0 if scenario<8 else -8 if scenario==10 else -10)
                self.assertEqual((get('cached_checks'),get('full_prepares')),
                                 (1,int(scenario in (7,9,10))))
                self.assertEqual((get('starts'),get('installs'),get('crash_starts')),(0,0,0))
                if scenario!=10:
                    lib.portable_assets_setup_tick()
                    self.assert_silent_counters(lib)
                    self.assertTrue(all(p==0x12345678 for p in screen))
                    self.assertEqual(self.files_snapshot(),snapshot)
                else:
                    self.assertTrue(any(p!=0x12345678 for p in screen))
                    self.assertIn('APPLICATION status=-1 detail=bundle',Path('data/log/setup.log').read_text())
                    for name in ('asset_stats','asset_opens','manifest_opens','directory_writes'):
                        self.assertEqual(C.c_uint.in_dll(lib,'portable_test_'+name).value,0,name)
        Path('data/assets/assets-manifest.txt').unlink()
        # The direct path deliberately trusts a completed marker. Corrupt it
        # to exercise first-install/repair UI and worker failure paths below.
        incomplete=completed.replace(b'JGR_CHECKED 1',b'JGR_CHECKED 2')
        for scenario in range(1,6):
            with self.subTest(scenario=scenario):
                seal.write_bytes(incomplete)
                lib.lifecycle_reset(scenario)
                status=lib.portable_assets_bootstrap()
                self.assertLess(status,0)
                if get('installs'):
                    self.assertEqual(get('seal_at_install'),0)
                    self.assertFalse(seal.exists())
                if scenario<=2:
                    self.assertEqual((get('starts'),get('installs'),get('stops')),(0,0,0))
                    self.assertEqual(get('crash_starts'),0)
                else:
                    self.assertEqual((get('ordered'),get('starts'),get('installs'),get('stops')),(1,1,1,1))
                    self.assertEqual((get('crash_starts'),get('crash_ordered')),(1,1))
                if scenario==4:
                    self.assertEqual((status,get('progress_result'),get('notes')),(-10,0,1))
                if scenario==3:self.assertEqual(get('progress_result'),1,'Unsupported ME must keep CPU conversion available')
        seal.write_bytes(incomplete)
        C.c_uint.in_dll(lib,'portable_test_seal_remove_fail').value=1
        lib.lifecycle_reset(0)
        self.assertEqual(lib.portable_assets_bootstrap(),-8)
        self.assertEqual((get('starts'),get('installs'),get('stops')),(0,0,0))
        self.assertEqual(seal.read_bytes(),incomplete)
        log=Path('data/log/setup.log').read_text()
        self.assertIn('queued=4 published=3 overlapped_jobs=2 drain_us=123',log)
        self.assertIn('jobs=7 completed=7',log)
        self.assertIn('cache_us=31 wait_us=41',log)
        self.assertIn('SETUP_CRASH status=-1 build=fixture',log)

    def test_missing_manifest_blocks_asset_access_before_any_original_loader(self):
        Path('data/assets/assets-manifest.txt').unlink()
        Path('font.bin').write_bytes(b'legacy')
        result = self.init()
        self.assertEqual(result.status, -1)
        self.assertEqual(self.lib.portable_test_read(b'font.bin'), -1)
        self.assertFalse(Path('data/cache/assets.checked').exists())

    def test_wrong_format_layout_identity_and_missing_records_are_rejected(self):
        original = manifest()
        for data, expected in [(original.replace(b'JGR_ASSETS 1', b'JGR_ASSETS 2'), -3),
                               (original.replace(b'layout 2', b'layout 1'), -3),
                               (original.replace(b'count 3', b'count 2'), -4),
                               (original[:-1], -4),
                               (original + b'extra\n', -4),
                               (original.replace(b'font.bin', b'evil.bin'), -4)]:
            with self.subTest(data=data):
                Path('data/assets/assets-manifest.txt').write_bytes(data)
                self.assertEqual(self.init().status, expected)
                self.assertEqual(self.lib.portable_test_read(b'font.bin'), -1)

    def test_missing_truncated_and_same_size_corruption(self):
        path = Path('data/assets/music1.adx')
        for change, expected in [(None, -5), (b'x', -6), (b'x' * len(DATA['music1.adx']), -7)]:
            with self.subTest(expected=expected):
                if change is None:
                    path.unlink()
                else:
                    path.write_bytes(change)
                result = self.init()
                self.assertEqual(result.status, expected)
                self.assertEqual(result.file, b'music1.adx')
                self.assertFalse(Path('data/cache/assets.checked').exists())

    def test_stale_partial_and_wrong_identity_seals_cannot_skip_verification(self):
        self.assertEqual(self.init().status, 0)
        seal = Path('data/cache/assets.checked')
        good = seal.read_bytes()
        for invalid in [b'', good[:-1], good + b'extra',good[:-9]+b'zzzzzzzz\n',
                        good[:-9]+b'0000000\n',good[:-9]+b'000000000\n',
                        good.replace(b' 2 3 ',b' 1 3 '),good.replace(b' 2 3 ',b' 2 4 '),
                        good.replace(b'JGR_CHECKED 1', b'JGR_CHECKED 2'),
                        good.replace(f'{zlib.crc32(manifest()):08x}'.encode(), b'00000000')]:
            with self.subTest(invalid=invalid):
                seal.write_bytes(invalid)
                result = self.init()
                self.assertEqual((result.status, result.full_verification, result.cache_written), (0, 1, 1))
        seal.unlink()
        Path('data/cache/assets.checked.tmp').write_bytes(good)
        self.assertEqual(self.init().full_verification, 1)

    def test_explicit_reverify_detects_same_size_corruption(self):
        self.assertEqual(self.init().status, 0)
        path = Path('data/assets/music1.adx')
        old_time = path.stat().st_mtime
        path.write_bytes(b'x' * len(DATA['music1.adx']))
        os.utime(path, (old_time + 4, old_time + 4))  # exceeds FAT timestamp resolution
        # Completed installs deliberately defer external changes to a repair
        # request or a required-data loader error, avoiding per-file stats.
        self.assertEqual((self.init().status,self.init().full_verification),(0,0))
        Path('data/cache/assets.checked').unlink()
        result = self.init()
        self.assertEqual((result.status, result.full_verification), (-7, 1))

    def test_required_data_error_invalidates_seal_and_repairs_missing_or_resized_asset(self):
        self.assertEqual(self.init().status, 0)
        path = Path('data/assets/music1.adx')
        path.unlink()
        self.assertEqual(self.init().status,0)
        self.lib.portable_assets_data_error()
        self.assertFalse(Path('data/cache/assets.checked').exists())
        self.assertEqual(self.lib.portable_test_read(b'font.bin'),-1)
        self.assertEqual(self.init().status, -5)
        path.write_bytes(b'x')
        self.assertEqual(self.init().status, -6)

    def test_path_safety_asset_immutability_and_rejected_old_root(self):
        self.assertEqual(self.init().status, 0)
        Path('font.bin').write_bytes(b'L')
        for name in [b'font.bin', b'data/assets/font.bin']:
            self.assertEqual(self.lib.portable_test_read(name), ord('F'))
        for name in [b'../font.bin', b'data/assets/../../font.bin', b'/tmp/font.bin',
                     b'ms0:/OTHER/font.bin', b'ms0:/PSP/GAME/JGRVIEW/font.bin', b'grf//s00.bin', b'grf/./s00.bin', b'font.bin/',
                     b'x' * 193, b'font\\bin', b'font bin']:
            with self.subTest(name=name):
                self.assertEqual(self.lib.portable_test_read(name), -1)
        self.assertEqual(self.lib.portable_test_write(b'font.bin', b'wrong'), -1)
        self.assertEqual(self.lib.portable_test_remove(b'font.bin'), -1)
        self.assertEqual(self.lib.portable_test_rename(b'font.bin', b'data/cache/font.bin'), -1)
        self.assertEqual(Path('data/assets/font.bin').read_bytes(), DATA['font.bin'])

    def test_log_cache_paths_and_saves_require_psp_device(self):
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertLess(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertLess(self.lib.story_load(b'jgr_story.sav'), 0)
        self.assertFalse(Path('jgr_story.sav').exists())
        self.assertEqual(self.lib.portable_test_write(b'jgr_log.txt', b'report'), 0)
        self.assertEqual(self.lib.portable_test_rename(b'jgr_log.txt', b'jgr_log_prev.txt'), 0)
        self.assertEqual(Path('data/log/jgr_log_prev.txt').read_bytes(), b'report')
        self.assertEqual(self.lib.portable_test_write(b'shot_fixture.raw', b'pixels'), 0)
        self.assertEqual(Path('data/cache/shot_fixture.raw').read_bytes(), b'pixels')
        self.assertEqual(self.lib.portable_test_write(b'shot.raw', b'frame'), 0)
        self.assertEqual(Path('data/cache/shot.raw').read_bytes(), b'frame')
        Path('data/cache/autopilot').write_bytes(b'neutral')
        self.assertEqual(self.lib.portable_test_read(b'autopilot'), ord('n'))

    def test_cached_absolute_root_supports_worker_with_different_cwd(self):
        self.assertEqual(self.init().status, 0)
        app = Path.cwd()
        child = app / 'thread-cwd'
        child.mkdir()
        os.chdir(child)
        self.assertEqual(self.lib.portable_test_read(b'font.bin'), ord('F'))
        self.assertEqual(self.lib.portable_test_write(str(app / 'jgr_hang.txt').encode(), b'failure'), 0)
        self.assertEqual((app / 'data/log/jgr_hang.txt').read_bytes(), b'failure')

    def device_application(self, device, folder='JGR_PORTABLE'):
        # "ms0:" and "ef0:" are ordinary subdirectories of this case's temp
        # cwd on the host; the getcwd seam supplies PSP spellings to runtime.
        app = Path(f'{device}:/PSP/GAME/{folder}')
        for name, data in DATA.items():
            path = app / 'data/assets' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (app / 'data/assets/assets-manifest.txt').write_bytes(manifest())
        self.app_root.value=str(app).encode()
        return app

    def test_psp_saves_use_same_device_outside_application_with_backup_recovery(self):
        for device in ('ms0', 'ef0'):
            with self.subTest(device=device):
                app = self.device_application(device)
                self.assertEqual(self.init().status, 0)
                destination = Path(f'{device}:/PSP/SAVEDATA/JGR_PORTABLE')
                self.assertFalse(destination.exists())
                self.assertLess(self.lib.story_load(b'jgr_story.sav'), 0)
                self.assertFalse(destination.exists())
                self.lib.story_reset()
                for n in (49, 50, 51):
                    self.story[0x300] = n
                    self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
                    self.lib.story_reset()
                    self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
                    self.assertEqual(self.story[0x300], n)
                self.assertEqual((destination / 'jgr_story.sav').read_bytes()[0x300], 51)
                self.assertEqual((destination / 'jgr_story.sav.bak').read_bytes()[0x300], 50)
                self.assertFalse((destination / 'jgr_story.sav.tmp').exists())
                (destination / 'jgr_story.sav').write_bytes(b'damaged')
                self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
                self.assertEqual(self.story[0x300], 50)
                self.story[0x300] = 52
                self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
                self.assertEqual((destination / 'jgr_story.sav.bak').read_bytes()[0x300], 50)
                for name in (b'jgr_story.sav',
                             str(destination / 'jgr_story.sav').encode()):
                    resolved = C.create_string_buffer(768)
                    self.assertEqual(self.lib.portable_assets_resolve(resolved, name, 0), 0)
                    self.assertEqual(resolved.value, str(destination / 'jgr_story.sav').encode())
                other = 'ef0' if device == 'ms0' else 'ms0'
                for name in (f'{other}:/PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav',
                             f'{destination}/../other/jgr_story.sav'):
                    self.assertLess(self.lib.portable_assets_resolve(resolved, name.encode(), 0), 0)
                self.assertEqual(self.lib.portable_test_sce_stat(b'jgr_story.sav'), 0)
                self.assertGreaterEqual(self.lib.portable_test_sce_write_open(b'jgr_story.sav.tmp', 0x602), 0)
                io_save = str(destination / 'io.sav').encode()
                self.assertEqual(self.lib.portable_test_sce_rename(b'jgr_story.sav.tmp', io_save), 0)
                self.assertEqual(self.lib.portable_test_sce_remove(io_save), 0)

    def test_saved_story_bytes_load_without_repairs(self):
        self.device_application('ms0')
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.story[0x32c], 1)
        self.story[0x32c] = 0
        saved = bytes(self.story)
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
        self.assertEqual(bytes(self.story), saved)

    def save_eboot(self, app, icon=None):
        if icon is None:
            icon = (ROOT / 'icon/ICON0.png').read_bytes()
        # Only the container's ICON0 section matters for save presentation.
        end = 40 + len(icon)
        (app / 'EBOOT.PBP').write_bytes(
            b'\0PBP' + struct.pack('<9I', 0x10000, 40, 40, *([end] * 6)) + icon)
        return icon

    def save_metadata(self, saved):
        directory = self.app_root.value[:4].decode() + '/PSP/SAVEDATA/JGR_PORTABLE'
        executable = self.app_root.value + b'/EBOOT.PBP'
        return self.lib.savedata_update(directory.encode(), executable, saved)

    def read_save_sfo(self, path):
        data = path.read_bytes()
        magic, version, keys, values, count = struct.unpack_from('<4s4I', data)
        self.assertEqual((magic, version, count), (b'\0PSF', 0x101, 8))
        self.assertEqual(keys, 20 + 16 * count)
        self.assertEqual(values % 4, 0)
        fields = {}
        for i in range(count):
            key, fmt, size, capacity, offset = struct.unpack_from('<HHIII', data, 20 + 16*i)
            self.assertLessEqual(size, capacity)
            self.assertEqual(offset % 4, 0)
            self.assertLessEqual(values + offset + capacity, len(data))
            end = data.index(0, keys + key, values)
            name = data[keys + key:end].decode('ascii')
            self.assertNotIn(name, fields)
            value = data[values + offset:values + offset + size]
            self.assertEqual(data[values + offset + size:values + offset + capacity],
                             bytes(capacity - size))
            if fmt == 0x0204:
                self.assertEqual(value[-1:], b'\0')
                value = value[:-1].decode('utf-8')
            elif fmt == 0x0404:
                self.assertEqual(size, 4)
                value, = struct.unpack('<I', value)
            else:
                self.assertEqual(fmt, 0x0004)
            fields[name] = value
        self.assertEqual(list(fields), sorted(fields))
        return fields

    def check_save_integrity_calls(self, path):
        # The firmware mock supplies distinguishable hashes. Verify what the
        # production writer asked the firmware to hash, including ordering,
        # zeroed hash slots, the flag and all bytes of the completed SFO.
        data = bytearray(path.read_bytes())
        params = self.read_save_sfo(path)['SAVEDATA_PARAMS']
        self.assertEqual(params, b'\1' + bytes(15) + bytes(range(16, 32)) +
                         bytes(range(32, 48)) + bytes(80))
        self.assertEqual(self.hash_bad.value, 0)
        self.assertEqual(list(self.hash_bytes), [len(data), len(data)])
        start = data.index(params)
        data[start + 16:start + 32] = bytes(16)
        self.assertEqual(bytes(self.hash_input[0][:len(data)]), bytes(data))
        data[start:start + 128] = bytes(128)
        self.assertEqual(bytes(self.hash_input[1][:len(data)]), bytes(data))

    def test_save_xmb_metadata_and_icon_preserve_progress_on_both_devices(self):
        for device in ('ms0', 'ef0'):
            with self.subTest(device=device):
                app = self.device_application(device)
                icon = self.save_eboot(app)
                self.assertEqual(self.init().status, 0)
                destination = Path(f'{device}:/PSP/SAVEDATA/JGR_PORTABLE')
                self.assertLess(self.save_metadata(0), 0)
                self.assertFalse(destination.exists())
                self.lib.story_reset()
                for n in (71, 72):
                    self.story[0x300] = n
                    self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
                progress = {p.name: p.read_bytes() for p in destination.iterdir()}
                self.assertEqual(self.save_metadata(1), 0)
                fields = self.read_save_sfo(destination / 'PARAM.SFO')
                fields.pop('SAVEDATA_PARAMS')
                self.assertEqual(fields, {
                    'CATEGORY': 'MS', 'PARENTAL_LEVEL': 0,
                    'SAVEDATA_DETAIL': 'Story progress and settings.',
                    'SAVEDATA_DIRECTORY': 'JGR_PORTABLE',
                    'SAVEDATA_FILE_LIST': bytes(3168),
                    'SAVEDATA_TITLE': 'Story progress', 'TITLE': 'JGR Portable',
                })
                self.check_save_integrity_calls(destination / 'PARAM.SFO')
                self.assertEqual((destination / 'ICON0.PNG').read_bytes(), icon)
                for name, data in progress.items():
                    self.assertEqual((destination / name).read_bytes(), data)
                self.assertFalse(list(destination.glob('*.tmp')))
                # Loading a save adds missing presentation, but a later load
                # neither rewrites it nor changes its displayed save timestamp.
                (destination / 'PARAM.SFO').unlink()
                self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
                self.assertEqual(self.save_metadata(0), 0)
                self.reset_silent_counters()
                self.assertEqual(self.save_metadata(0), 0)
                self.assert_silent_counters()
                os.utime(destination / 'PARAM.SFO', (1000000000, 1000000000))
                self.assertEqual(self.save_metadata(1), 0)
                self.assertGreater((destination / 'PARAM.SFO').stat().st_mtime, 1000000000)
                self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
                self.assertEqual(self.story[0x300], 72)

    def test_save_xmb_rejects_damaged_packaged_icon_without_touching_save(self):
        app = self.device_application('ms0')
        icon = self.save_eboot(app)
        good = (app / 'EBOOT.PBP').read_bytes()
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        progress = (destination / 'jgr_story.sav').read_bytes()
        wrong_dimensions = bytearray(good)
        struct.pack_into('>I', wrong_dimensions, 40 + 16, 480)
        bad_offsets = bytearray(good)
        struct.pack_into('<I', bad_offsets, 16, 39)
        oversize = b'\0PBP' + struct.pack('<9I', 0x10000, 40, 40, *([40+131073]*6)) + bytes(131073)
        for broken in (good[:20], good[:-1], b'bad!' + good[4:], wrong_dimensions, bad_offsets, oversize):
            with self.subTest(bytes=len(broken)):
                (app / 'EBOOT.PBP').write_bytes(broken)
                self.assertLess(self.save_metadata(0), 0)
                self.assertEqual((destination / 'ICON0.PNG').read_bytes(), icon)
                self.assertEqual((destination / 'jgr_story.sav').read_bytes(), progress)
                self.assertFalse(list(destination.glob('*.tmp')))

    def test_save_xmb_write_failure_does_not_block_story_saving(self):
        app = self.device_application('ms0')
        self.save_eboot(app)
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        previous = (destination / 'jgr_story.sav').read_bytes()
        sfo = (destination / 'PARAM.SFO').read_bytes()
        (destination / 'PARAM.SFO.tmp').mkdir()
        self.story[0x300] = 91
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertLess(self.save_metadata(1), 0)
        self.assertEqual((destination / 'PARAM.SFO').read_bytes(), sfo)
        self.assertEqual((destination / 'jgr_story.sav.bak').read_bytes(), previous)
        self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
        self.assertEqual(self.story[0x300], 91)

    def test_save_xmb_artwork_free_development_build(self):
        app = self.device_application('ms0')
        self.save_eboot(app, b'')
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        self.assertEqual(self.read_save_sfo(destination / 'PARAM.SFO')['CATEGORY'], 'MS')
        self.assertFalse((destination / 'ICON0.PNG').exists())

    def test_save_xmb_repairs_missing_integrity_hashes_on_load(self):
        app = self.device_application('ms0')
        self.save_eboot(app)
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        path = destination / 'PARAM.SFO'
        expected = path.read_bytes()
        params = self.read_save_sfo(path)['SAVEDATA_PARAMS']
        path.write_bytes(expected.replace(params, bytes(128)))
        progress = (destination / 'jgr_story.sav').read_bytes()
        self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        self.assertEqual(path.read_bytes(), expected)
        self.check_save_integrity_calls(path)
        self.assertEqual((destination / 'jgr_story.sav').read_bytes(), progress)

    def test_save_xmb_hash_failure_preserves_existing_files(self):
        app = self.device_application('ms0')
        self.save_eboot(app)
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        expected = {p.name: p.read_bytes() for p in destination.iterdir()}
        for step in range(1, 7):
            with self.subTest(failed_firmware_call=step):
                self.hash_step.value = 0
                self.hash_fail.value = step
                self.assertLess(self.save_metadata(1), 0)
                self.assertEqual(self.hash_step.value, step)
                self.assertEqual({p.name: p.read_bytes() for p in destination.iterdir()}, expected)
                self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
        self.hash_fail.value = 0

    def test_save_xmb_loads_hash_service_and_reuses_resident_service(self):
        app = self.device_application('ms0')
        self.save_eboot(app)
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        self.assertEqual(self.service_calls.value, 2)
        self.assertEqual(self.service_started.value, 1)
        self.assertEqual(self.save_metadata(0), 0)
        self.assertEqual(self.service_calls.value, 2)
        self.lib.savedata_test_reset_service()
        self.assertEqual(self.save_metadata(0), 0)
        self.assertEqual(self.service_calls.value, 3)
        self.check_save_integrity_calls(Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE/PARAM.SFO'))

    def test_save_xmb_rejects_unavailable_service_and_empty_hashes(self):
        app = self.device_application('ms0')
        self.save_eboot(app)
        self.assertEqual(self.init().status, 0)
        self.lib.story_reset()
        self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(self.save_metadata(0), 0)
        destination = Path('ms0:/PSP/SAVEDATA/JGR_PORTABLE')
        expected = {p.name: p.read_bytes() for p in destination.iterdir()}
        for failure in range(1, 4):
            with self.subTest(service_failure=failure):
                self.lib.savedata_test_reset_service()
                self.service_loaded.value = self.service_started.value = 0
                self.service_fail.value = failure
                self.assertLess(self.save_metadata(1), 0)
                self.assertEqual({p.name: p.read_bytes() for p in destination.iterdir()}, expected)
        self.service_fail.value = 0
        self.service_loaded.value = self.service_started.value = 0
        for noop in range(1, 4):
            with self.subTest(noop_firmware_call=noop):
                self.hash_noop.value = noop
                self.assertLess(self.save_metadata(1), 0)
                self.assertEqual({p.name: p.read_bytes() for p in destination.iterdir()}, expected)
        self.hash_noop.value = 0
        self.assertEqual(self.save_metadata(0), 0)

    def test_cached_psp_boot_uses_external_saves_without_asset_reads_or_writes(self):
        for device in ('ms0', 'ef0'):
            with self.subTest(device=device):
                app = self.device_application(device)
                self.assertEqual(self.init().status, 0)
                # Reset the captured root to the host path, then exercise the
                # completed-install shortcut that does not call assets_init.
                self.app_root.value=b''
                self.assertEqual(self.init().status, 0)
                self.app_root.value=str(app).encode()
                self.reset_silent_counters()
                self.assertEqual(self.lib.portable_assets_bootstrap(), 0)
                self.assert_silent_counters()
                self.lib.story_reset()
                self.story[0x300]=61
                self.assertEqual(self.lib.story_save(b'jgr_story.sav'), 0)
                destination = Path(f'{device}:/PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav')
                self.assertEqual(destination.read_bytes()[0x300], 61)
                self.assertEqual({p.name for p in (app / 'data').iterdir()}, {'assets', 'cache', 'log'})
                # A different install on the same device sees the same save.
                self.device_application(device, 'Replacement')
                self.assertEqual(self.init().status, 0)
                self.assertEqual(self.lib.story_load(b'jgr_story.sav'), 0)
                self.assertEqual(self.story[0x300], 61)

    def test_psp_save_directory_failure_leaves_assets_and_receipt_usable(self):
        app = self.device_application('ms0')
        self.assertEqual(self.init().status, 0)
        receipt = app / 'data/cache/assets.checked'
        before = receipt.read_bytes()
        Path('ms0:/PSP/SAVEDATA').write_bytes(b'not a directory')
        self.reset_silent_counters()
        self.assertEqual(self.lib.portable_assets_bootstrap(), 0)
        self.assert_silent_counters()
        self.lib.story_reset()
        self.assertLess(self.lib.story_save(b'jgr_story.sav'), 0)
        self.assertEqual(receipt.read_bytes(), before)
        self.assertEqual(self.lib.portable_test_read(b'font.bin'), ord('F'))
        self.assertEqual({p.name for p in (app / 'data').iterdir()}, {'assets', 'cache', 'log'})

    def test_native_psp_read_flags_and_report_io_use_same_policy(self):
        self.assertEqual(self.init().status, 0)
        self.assertEqual(self.lib.portable_test_sce_read(b'font.bin'), ord('F'))
        self.assertEqual(self.lib.portable_test_sce_stat(b'font.bin'), 0)
        for flags in [2, 3, 0x202, 0x402, 0x102]:
            self.assertLess(self.lib.portable_test_sce_write_open(b'font.bin', flags), 0)
        self.assertGreaterEqual(self.lib.portable_test_sce_write_open(b'jgr_log.txt', 0x602), 0)
        self.assertEqual(self.lib.portable_test_sce_rename(b'jgr_log.txt', b'jgr_log_prev.txt'), 0)
        self.assertEqual(self.lib.portable_test_sce_stat(b'jgr_log_prev.txt'), 0)
        self.assertTrue(Path('data/log/jgr_log_prev.txt').exists())
        self.assertEqual(self.lib.portable_test_sce_remove(b'jgr_log_prev.txt'), 0)
        self.assertFalse(Path('data/log/jgr_log_prev.txt').exists())

    def assert_screen_bounds(self):
        untouched = 0x12345678
        self.assertTrue(all(p == untouched for p in self.screen[:512]))
        self.assertTrue(all(p == untouched for p in self.screen[-512:]))
        for row in range(1, 273):
            self.assertTrue(all(p == untouched for p in self.screen[row * 512 + 480:(row + 1) * 512]))

    def test_setup_screen_first_and_cached_boot_stays_inside_framebuffer(self):
        for cached in (False, True):
            with self.subTest(cached=cached):
                self.screen[:] = [0x12345678] * len(self.screen)
                self.assertEqual(self.lib.portable_assets_bootstrap(), 0)
                self.assert_screen_bounds()
                if cached:
                    self.assertTrue(all(p==0x12345678 for p in self.screen))
                else:
                    for row in range(196, 208):
                        self.assertEqual(list(self.screen[(row + 1) * 512 + 60:(row + 1) * 512 + 420]),
                                         [0xff00d6ff] * 360)

    def test_setup_error_screens_stay_inside_framebuffer_and_block_boot(self):
        for path in ('data/assets/music1.adx', 'data/assets/assets-manifest.txt'):
            with self.subTest(path=path):
                self.screen[:] = [0x12345678] * len(self.screen)
                Path(path).unlink()
                self.assertLess(self.lib.portable_assets_bootstrap(), 0)
                self.assert_screen_bounds()
                self.assertEqual(self.lib.portable_test_read(b'font.bin'), -1)
        self.screen[:] = [0x12345678] * len(self.screen)
        self.lib.portable_assets_data_error()
        self.assert_screen_bounds()

    def background(self,color=0x001f):
        payload=struct.pack('<H',color)*(480*272)
        return struct.pack('<8s4I',b'JGRBG01\0',480,272,len(payload),zlib.crc32(payload))+payload

    def write_background(self,data,name='background.jbg'):
        path=Path('data/setup')/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)

    def assert_background_pixels(self,color):
        for x,y in ((0,0),(479,271),(10,160),(10,202),(10,220)):
            self.assertEqual(self.screen[(y+1)*512+x],color,(x,y))

    def test_startup_picture_needs_no_manifest_scan_or_persistent_heap(self):
        self.assertEqual(self.init().status,0)
        self.write_background(self.background(0xf800)) # PIC1 must not replace the warning.
        self.write_background(self.background(),'startup-warning.jbg')
        before=self.files_snapshot()
        self.screen[:]=[0x12345678]*len(self.screen)
        self.reset_silent_counters()
        self.lib.portable_assets_show_startup()
        self.assert_background_pixels(0xff0000ff)
        for y in range(272):
            self.assertEqual(list(self.screen[(y+1)*512:(y+1)*512+480]),[0xff0000ff]*480,
                             'Warning must have full brightness and no heading/text overlay')
        self.assert_screen_bounds()
        self.assertEqual(self.background_live.value,0)
        for name in ('asset_stats','asset_opens','manifest_opens','file_writes','directory_writes'):
            self.assertEqual(C.c_uint.in_dll(self.lib,'portable_test_'+name).value,0,name)
        self.assertEqual(self.files_snapshot(),before)
        picture=list(self.screen)
        self.lib.portable_assets_setup_tick()
        self.assertEqual(list(self.screen),picture,'No callback may repaint a released startup image')

    def test_startup_picture_fallback_does_not_block_boot(self):
        self.write_background(self.background(0xf800))
        good=self.background();bad_crc=bytearray(good);bad_crc[-1]^=1
        for background in (None,b'bad',good[:-1],good+b'extra',bytes(bad_crc),good):
            with self.subTest(background_bytes=len(background) if background else 0):
                if background is not None:self.write_background(background,'startup-warning.jbg')
                self.background_fail_alloc.value=background==self.background()
                self.screen[:]=[0x12345678]*len(self.screen)
                self.lib.portable_assets_show_startup()
                self.assert_background_pixels(0xff000000)
                self.assert_screen_bounds()
                self.assertEqual(self.background_live.value,0)
                self.assertEqual(self.lib.portable_assets_bootstrap(),0)

    def test_optional_background_survives_progress_redraw_and_is_freed_before_return(self):
        self.write_background(self.background())
        for cached in (False,True):
            with self.subTest(cached=cached):
                self.screen[:]=[0x12345678]*len(self.screen)
                self.reset_silent_counters()
                self.assertEqual(self.lib.portable_assets_bootstrap(),0)
                if cached:
                    self.assert_silent_counters()
                    self.assertTrue(all(p==0x12345678 for p in self.screen))
                else:
                    self.assert_background_pixels(0xff000099)
                    self.assertEqual(self.background_size.value,480*272*2)
                self.assert_screen_bounds()
                self.assertEqual(self.background_live.value,0)
        self.assertFalse(Path('data/assets/background.jbg').exists())

    def test_setup_failure_and_generated_data_error_draw_and_release_optional_background(self):
        self.write_background(self.background())
        Path('data/assets/assets-manifest.txt').unlink()
        self.assertEqual(self.lib.portable_assets_bootstrap(),-1)
        self.assert_background_pixels(0xff000099)
        self.assertEqual(self.background_live.value,0)
        self.write_background(self.background(0xf800))
        self.lib.portable_assets_data_error()
        self.assert_background_pixels(0xff990000)
        self.assertEqual(self.background_live.value,0)

    def test_bad_optional_background_falls_back_without_blocking_valid_assets(self):
        good=self.background();cases=[b'',good[:23],good[:-1],good+b'extra']
        for offset,value in ((0,b'WRONG!!!'),(8,struct.pack('<I',481)),(12,struct.pack('<I',271)),
                             (16,struct.pack('<I',0xffffffff)),(20,struct.pack('<I',0))):
            changed=bytearray(good);changed[offset:offset+len(value)]=value;cases.append(changed)
        for i,data in enumerate(cases):
            with self.subTest(case=i):
                seal=Path('data/cache/assets.checked')
                if seal.exists():seal.unlink()
                self.write_background(data)
                self.assertEqual(self.lib.portable_assets_bootstrap(),0)
                self.assert_background_pixels(0xff101010)
                self.assertEqual(self.background_live.value,0)

    def test_optional_background_allocation_failure_uses_plain_screen(self):
        self.write_background(self.background())
        self.background_fail_alloc.value=1
        self.assertEqual(self.lib.portable_assets_bootstrap(),0)
        self.assert_background_pixels(0xff101010)
        self.assertEqual(self.background_live.value,0)


if __name__ == '__main__':
    unittest.main()
