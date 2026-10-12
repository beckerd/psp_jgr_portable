"""City failure context preserves the first failed boundary without log I/O."""
import ctypes as C
import errno
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Failure(C.Structure):
    _fields_ = [('stage', C.c_uint), ('step', C.c_uint), ('section', C.c_uint),
                ('error', C.c_int), ('source_offset', C.c_int64), ('output_offset', C.c_int64),
                ('source_error', C.c_int), ('output_error', C.c_int),
                ('heap_free', C.c_uint), ('kernel_free', C.c_uint), ('kernel_max_free', C.c_uint)]


BRIDGE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "portable_scratch.h"
static unsigned scratch_opens;
static FILE *counted_open(JgrScratchPool *pool,const char *path){scratch_opens++;return jgr_scratch_open(pool,path);}
static FILE *fail_file,*caller_keys;static unsigned fail_close,caller_keys_closed;static unsigned clocks;
static size_t failing_write(const void *p,size_t n,size_t c,FILE *f){
 if(f==fail_file){errno=EIO;return 0;}return fwrite(p,n,c,f);
}
static int failing_close(FILE *f){if(f==caller_keys)caller_keys_closed=1;int result=fclose(f);if(fail_close){fail_close=0;errno=ENOSPC;return -1;}return result;}
#define fwrite failing_write
#define fclose failing_close
#define jgr_scratch_open counted_open
#include "portable_stage.c"
#undef jgr_scratch_open
#undef fwrite
#undef fclose
uint64_t jgr_stage_test_clock(void){clocks++;return 10;}
unsigned clock_calls(void){return clocks;}
int synthetic(void){
 FILE *source=tmpfile(),*out=tmpfile();if(!source||!out)return -1;
 fwrite("123456789",1,9,source);fwrite("abcd",1,4,out);fflush(source);fflush(out);
 JgrStageStatsScope a,b;jgr_stage_stats_begin(&a,2,JGR_STAGE_JOB_CITY);jgr_stage_stats_begin(&b,2,JGR_STAGE_JOB_RENDER);
 errno=EIO;stage_failed(2,JGR_STAGE_FAILURE_KEY_COPY,6,source,out);if(errno!=EIO)return -2;
 errno=ENOSPC;stage_failed(1,JGR_STAGE_FAILURE_SECTION_CLOSE,3,NULL,NULL);if(errno!=ENOSPC)return -3;
 jgr_stage_stats_end(&b,-1);jgr_stage_stats_end(&a,-1);fclose(source);fclose(out);return 0;
}
void next_scope(void){JgrStageStatsScope s;jgr_stage_stats_begin(&s,1,JGR_STAGE_JOB_OBJECTS);jgr_stage_stats_end(&s,0);}
int original(const char *gdi,const char *folder,unsigned mode){
 JgrDisc *disc=NULL;char error[256];if(jgr_disc_open(&disc,gdi,error,sizeof error))return -2;
 FILE *raw=tmpfile(),*owners=tmpfile(),*keys=tmpfile();if(!raw||!owners||!keys)return -3;
 caller_keys=keys;caller_keys_closed=0;fail_file=mode==1?owners:mode==2?keys:NULL;fail_close=mode==1||mode==3;
 int result=convert_stage(disc,2,folder,raw,2,owners,NULL,0,keys,NULL);int error_number=errno;
 fail_file=NULL;fail_close=0;if(caller_keys_closed)return -7;if(fseek(keys,0,SEEK_END))return -8;fclose(raw);fclose(owners);fclose(keys);caller_keys=NULL;jgr_disc_close(disc);errno=error_number;return result;
}

unsigned opened_scratch(void){return scratch_opens;}
int bank(const char *gdi,const char *folder,const char *raw_path,const char *owners_path,const char *key_path){
 JgrDisc *disc=NULL;char error[256];if(jgr_disc_open(&disc,gdi,error,sizeof error))return -2;
 FILE *raw=fopen(raw_path,"w+b"),*owners=fopen(owners_path,"w+b"),*keys=key_path?fopen(key_path,"w+b"):NULL;if(!raw||!owners||(key_path&&!keys))return -3;
 caller_keys=keys;caller_keys_closed=0;scratch_opens=0;fail_file=NULL;fail_close=0;
 int result=convert_stage(disc,2,folder,raw,2,owners,NULL,0,keys,NULL);
 if(caller_keys_closed)return -7;if(keys&&fseek(keys,0,SEEK_END))return -8;
 fclose(raw);fclose(owners);if(keys)fclose(keys);caller_keys=NULL;jgr_disc_close(disc);return result;
}
'''


class StageFailure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-stage-failure-')
        cls.work = Path(cls.temp.name); bridge = cls.work / 'bridge.c'; bridge.write_text(BRIDGE)
        cls.libs = []; cls.baseline = None
        baseline_path = os.environ.get('JGR_TEST_STAGE_KEYS_BASELINE')
        names = ('portable_stage_pack', 'portable_scratch', 'portable_models',
                 'portable_disc', 'portable_convert', 'portable_indexed')
        for enabled, baseline in [(0, False), (1, False)] + ([(1, True)] if baseline_path else []):
            out = cls.work / f'failure{enabled}-{baseline}.so'
            subprocess.run(['cc', '-std=gnu99', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', '-DJGR_STAGE_STATS_TESTING=1', f'-DJGR_STAGE_STATS={enabled}',
                            *(['-I' + str(Path(baseline_path).resolve())] if baseline else []),
                            '-I' + str(ROOT / 'psp/src'), str(bridge),
                            *[str(ROOT / 'psp/src' / (name + '.c')) for name in names], '-lm', '-o', str(out)], check=True)
            lib = C.CDLL(str(out), use_errno=True)
            lib.jgr_stage_failure.argtypes = [C.POINTER(Failure)]
            lib.original.argtypes = [C.c_char_p, C.c_char_p, C.c_uint]
            lib.bank.argtypes = [C.c_char_p] * 5
            if baseline:
                cls.baseline = lib
            else:
                cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def snapshot(self, lib):
        result = Failure(); lib.jgr_stage_failure(C.byref(result)); return result

    def test_first_error_and_offsets_survive_nested_cleanup_with_stats_disabled(self):
        for enabled, lib in enumerate(self.libs):
            lib.jgr_stage_stats_reset(); before = lib.clock_calls()
            self.assertEqual(lib.synthetic(), 0)
            f = self.snapshot(lib)
            self.assertEqual((f.stage, f.step, f.section, f.error), (2, 3, 6, errno.EIO))
            self.assertEqual((f.source_offset, f.output_offset), (9, 4))
            self.assertEqual((f.source_error, f.output_error), (0, 0))
            if not enabled:
                self.assertEqual(lib.clock_calls(), before)
            lib.next_scope(); self.assertEqual(bytes(self.snapshot(lib)), bytes(Failure()))

    def test_original_borrowed_key_stream_matches_previous_copy_and_remains_open(self):
        gdi = os.environ.get('JGR_TEST_GDI')
        if not gdi or not self.baseline:
            self.skipTest('Original disc and pre-change converter not supplied')
        outputs = []
        for label, lib, opens in (('before', self.baseline, 6), ('after', self.libs[1], 5)):
            paths = [self.work / f'{label}.{suffix}' for suffix in ('jgs', 'jrs', 'keys')]
            self.assertEqual(lib.bank(os.fsencode(gdi), os.fsencode(self.work), *map(os.fsencode, paths)), 0)
            self.assertEqual(lib.opened_scratch(), opens)
            outputs.append([p.read_bytes() for p in paths])
            self.assertFalse(list(self.work.glob('jgs-setup-*.tmp')))
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(len(outputs[1][2]), 3695688)
        raw, owners = self.work / 'local.jgs', self.work / 'local.jrs'
        self.assertEqual(self.libs[1].bank(os.fsencode(gdi), os.fsencode(self.work),
                                         os.fsencode(raw), os.fsencode(owners), None), 0)
        self.assertEqual(self.libs[1].opened_scratch(), 6)
        self.assertEqual([raw.read_bytes(), owners.read_bytes()], outputs[1][:2])
        self.assertFalse(list(self.work.glob('jgs-setup-*.tmp')))

    def test_original_city_postfinish_faults_report_the_exact_boundary(self):
        gdi = os.environ.get('JGR_TEST_GDI')
        if not gdi:
            self.skipTest('Original source disc not supplied')
        lib = self.libs[1]
        for mode, step, error in ((1, 2, errno.EIO), (2, 11, errno.EIO), (3, 4, errno.ENOSPC)):
            with self.subTest(mode=mode):
                self.assertEqual(lib.original(os.fsencode(gdi), os.fsencode(self.work), mode), -1)
                self.assertEqual(C.get_errno(), error)
                f = self.snapshot(lib)
                self.assertEqual((f.stage, f.step, f.error), (2, step, error))
                if mode == 2:
                    self.assertEqual((f.section, f.source_offset, f.output_offset), (6, -1, 0))
                self.assertFalse(list(self.work.glob('jgs-setup-*.tmp')))


if __name__ == '__main__':
    unittest.main()
