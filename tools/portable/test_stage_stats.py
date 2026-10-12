"""Coarse city timing: nested phases, snapshots, disabled builds and failures."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Stats(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in (
        'total_us', 'input_us', 'geometry_us', 'texture_us', 'assembly_us',
        'postprocess_us', 'current_us', 'objects', 'batches', 'vertices',
        'triangles', 'textures', 'output_bytes')]
    _fields_ += [(name, C.c_uint) for name in (
        'jobs', 'completed', 'failed', 'builds', 'active', 'stage', 'kind', 'phase')]


class Scope(C.Structure):
    _fields_ = [(name, C.c_uint) for name in ('active', 'stage', 'kind', 'phase')]


class StageStatsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix='jgr-stage-stats-')
        folder = Path(cls.folder.name)
        bridge = folder / 'bridge.c'
        bridge.write_text('''#include "portable_stage.h"
#include "portable_stage_props.h"
static uint64_t now;static unsigned clocks;
uint64_t jgr_stage_test_clock(void){clocks++;return now;}
void set_clock(uint64_t value){now=value;}
unsigned clock_calls(void){return clocks;}
int fail_input(void){FILE *out=tmpfile();if(!out)return -2;
 int result=jgr_stage_objects(NULL,1,".",out);fclose(out);return result;}
int city(const char *gdi,unsigned stage,const char *cache,const char *geometry,const char *ownership){
 JgrDisc *disc=NULL;char error[256];if(jgr_disc_open(&disc,gdi,error,sizeof error))return -2;
 FILE *out=fopen(geometry,"w+b"),*owners=fopen(ownership,"w+b");
 int result=out&&owners?jgr_stage_city(disc,stage,cache,out,owners):-3;
 if(out&&fclose(out))result=-4;if(owners&&fclose(owners))result=-4;
 jgr_disc_close(disc);return result;}
''')
        names = ('portable_stage', 'portable_stage_pack', 'portable_stage_props', 'portable_scratch',
                 'portable_models', 'portable_disc', 'portable_convert', 'portable_indexed')
        cls.libraries = []
        for enabled in (0, 1):
            output = folder / f'stats{enabled}.so'
            subprocess.run(['cc', '-std=gnu99', '-O2', '-ffp-contract=off', '-Wall',
                            '-Wextra', '-Werror', '-shared', '-fPIC',
                            f'-DJGR_STAGE_STATS={enabled}', '-DJGR_STAGE_STATS_TESTING=1',
                            '-I' + str(ROOT / 'psp/src'), str(bridge),
                            *[str(ROOT / 'psp/src' / (name + '.c')) for name in names],
                            '-lm', '-o', str(output)], check=True)
            lib = C.CDLL(str(output))
            lib.set_clock.argtypes = [C.c_uint64]
            lib.jgr_stage_stats.argtypes = [C.POINTER(Stats)]
            lib.jgr_stage_stats_begin.argtypes = [C.POINTER(Scope), C.c_uint, C.c_uint]
            lib.jgr_stage_stats_end.argtypes = [C.POINTER(Scope), C.c_int]
            lib.city.argtypes = [C.c_char_p, C.c_uint, C.c_char_p, C.c_char_p, C.c_char_p]
            cls.libraries.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def snapshot(self, lib):
        result = Stats(); lib.jgr_stage_stats(C.byref(result)); return result

    def test_nested_phases_are_exclusive_and_snapshot_does_not_charge_twice(self):
        lib = self.libraries[1]; lib.jgr_stage_stats_reset()
        outer, inner = Scope(), Scope()
        lib.set_clock(100); lib.jgr_stage_stats_begin(C.byref(outer), 1, 5)
        lib.set_clock(110); lib.jgr_stage_stats_phase(2)
        lib.set_clock(150); live = self.snapshot(lib)
        self.assertEqual((live.total_us, live.current_us, live.geometry_us), (50, 50, 40))
        self.assertEqual(bytes(live), bytes(self.snapshot(lib)))
        lib.set_clock(160); lib.jgr_stage_stats_begin(C.byref(inner), 2, 3)
        lib.set_clock(170); lib.jgr_stage_stats_phase(3)
        lib.set_clock(200); lib.jgr_stage_stats_end(C.byref(inner), -1)
        restored = self.snapshot(lib)
        self.assertEqual((restored.active, restored.stage, restored.kind, restored.phase), (1, 1, 5, 2))
        lib.set_clock(240); lib.jgr_stage_stats_phase(4)
        lib.set_clock(250); lib.jgr_stage_stats_end(C.byref(outer), -1)
        result = self.snapshot(lib)
        self.assertEqual((result.jobs, result.failed, result.completed, result.active), (1, 1, 0, 0))
        self.assertEqual((result.input_us, result.geometry_us, result.texture_us, result.assembly_us), (20, 90, 30, 10))
        self.assertEqual((result.total_us, result.current_us), (150, 150))
        self.assertEqual(result.total_us, result.input_us + result.geometry_us + result.texture_us + result.assembly_us + result.postprocess_us)
        lib.jgr_stage_stats_reset(); self.assertEqual(bytes(self.snapshot(lib)), bytes(Stats()))

    def test_disabled_has_no_clock_reads_or_statistics(self):
        lib = self.libraries[0]; lib.jgr_stage_stats_reset(); before = lib.clock_calls()
        scope = Scope(); lib.jgr_stage_stats_begin(C.byref(scope), 1, 5)
        lib.jgr_stage_stats_phase(3); lib.jgr_stage_stats_end(C.byref(scope), -1)
        self.assertEqual(lib.fail_input(), -1)
        self.assertEqual(bytes(self.snapshot(lib)), bytes(Stats()))
        self.assertEqual(lib.clock_calls(), before)

    def test_conversion_failure_closes_active_scope(self):
        lib = self.libraries[1]; lib.jgr_stage_stats_reset()
        self.assertEqual(lib.fail_input(), -1)
        result = self.snapshot(lib)
        self.assertEqual((result.jobs, result.failed, result.builds, result.active), (1, 1, 1, 0))
        self.assertEqual(result.output_bytes, 0)

    @unittest.skipUnless(os.environ.get('JGR_TEST_GDI'), 'Private supported disc required')
    def test_full_city_outputs_identical_with_statistics_enabled_or_disabled(self):
        catalog = {row['path']: row for row in json.loads(
            (ROOT / 'tools/portable/asset_catalog.json').read_text())['files']}
        folder = Path(self.folder.name)
        for stage in (1, 2, 3, 5, 6):
            for enabled, lib in enumerate(self.libraries):
                with self.subTest(stage=stage, enabled=enabled):
                    geometry, owners = folder / f'stage{stage}.jgs', folder / f'stage{stage}.jrs'
                    lib.jgr_stage_stats_reset()
                    self.assertEqual(lib.city(os.fsencode(os.environ['JGR_TEST_GDI']), stage,
                                              os.fsencode(folder), os.fsencode(geometry), os.fsencode(owners)), 0)
                    for output in (geometry, owners):
                        data = output.read_bytes(); expected = catalog[output.name]
                        self.assertEqual((len(data), hashlib.sha256(data).hexdigest()),
                                         (expected['bytes'], expected['sha256']))
                    stats = self.snapshot(lib)
                    if enabled:
                        self.assertEqual((stats.jobs, stats.completed, stats.failed, stats.active), (1, 1, 0, 0))
                        for name in ('builds', 'objects', 'batches', 'vertices', 'triangles', 'textures', 'output_bytes'):
                            self.assertGreater(getattr(stats, name), 0, name)
                    else:
                        self.assertEqual(bytes(stats), bytes(Stats()))


if __name__ == '__main__':
    unittest.main()
