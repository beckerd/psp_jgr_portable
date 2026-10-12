"""Fault-inject city-pair cleanup around the production orchestration function."""
import errno
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "portable_stage.h"
#define PATH_MAXIMUM 768
#define JGR_INSTALL_CANCELLED -5
#define JGR_INSTALL_CHECKSUM -4
typedef struct {unsigned op,bytes,arg[4];} Recipe;
typedef struct {int status;char file[192],detail[192];} Result;
typedef struct {Result *result;void *disc;} Installer;
static const char *mode;
static unsigned closes,flushes,cleanups,publishes,conversions;
static FILE *const output=(FILE *)(uintptr_t)1;
static FILE *const companion=(FILE *)(uintptr_t)2;
static int find_recipe(Installer *s,const char *name,Recipe *r,unsigned *index)
{(void)s;(void)name;(void)index;memset(r,0,sizeof *r);r->bytes=16;return 0;}
static int path(Installer *s,char *out,const char *prefix,const char *name)
{(void)s;snprintf(out,PATH_MAXIMUM,"%s%s",prefix,name);return 0;}
static int verified(Installer *s,const char *name,const Recipe *r,unsigned *crc)
{(void)s;(void)r;*crc=123;if(!strstr(name,".setup-tmp"))return !strncmp(mode,"cached",6);if(!strcmp(mode,"verify")){errno=EILSEQ;return 0;}return 1;}
static FILE *setup_output(const char *name){(void)name;return companion;}
static int fail(Installer *s,int code,const char *detail)
{s->result->status=code;snprintf(s->result->detail,sizeof s->result->detail,"%s",detail);return code;}
static int stream_output(Installer *s,FILE *in,unsigned bytes,FILE *out)
{(void)s;(void)in;(void)bytes;(void)out;if(!strcmp(mode,"cached-copy")){errno=EIO;return -1;}return 0;}
static int info(const char *name,struct stat *st)
{(void)name;memset(st,0,sizeof *st);st->st_mode=S_IFREG;return 0;}
static int jgr_stage_city(void *disc,unsigned stage,const char *directory,FILE *geometry,FILE *owners)
{(void)disc;(void)stage;(void)directory;(void)geometry;(void)owners;conversions++;
 if(!strcmp(mode,"convert")||!strcmp(mode,"convert-and-cleanup")||!strcmp(mode,"context")||!strcmp(mode,"context-max")||!strcmp(mode,"stale-context")||!strcmp(mode,"existing-detail")){errno=ENOMEM;return -1;}
 if(!strcmp(mode,"convert-no-errno")){errno=0;return -1;}return 0;}
void jgr_stage_failure(JgrStageFailure *failure)
{memset(failure,0,sizeof *failure);if(!strcmp(mode,"context-max")){
 failure->stage=failure->step=failure->section=UINT_MAX;failure->error=INT_MIN;
 failure->source_offset=failure->output_offset=INT64_MIN;failure->source_error=failure->output_error=INT_MIN;
 failure->heap_free=failure->kernel_free=failure->kernel_max_free=UINT_MAX;
 }else if(!strcmp(mode,"context")||!strcmp(mode,"stale-context")||!strcmp(mode,"existing-detail")){
 failure->stage=!strcmp(mode,"stale-context")?1:2;failure->step=JGR_STAGE_FAILURE_KEY_COPY;failure->section=6;
 failure->error=ENOSPC;failure->source_offset=123456;failure->output_offset=120000;failure->output_error=1;
 failure->heap_free=6000000;failure->kernel_free=500000;failure->kernel_max_free=200000;}}
static FILE *fake_open(const char *name,const char *how){(void)name;(void)how;return companion;}
static int fake_flush(FILE *file)
{(void)file;flushes++;if(!strcmp(mode,"flush")||!strcmp(mode,"flush-and-close")){errno=ENOSPC;return EOF;}errno=ESPIPE;return 0;}
static int fake_close(FILE *file)
{(void)file;closes++;if(!strcmp(mode,"close")||!strcmp(mode,"cached-close")||!strcmp(mode,"cached-copy")||!strcmp(mode,"flush-and-close")||!strcmp(mode,"convert-and-cleanup")){errno=EBADF;return EOF;}errno=EOVERFLOW;return 0;}
static int fake_remove(const char *name)
{if(strstr(name,".setup-tmp")){cleanups++;errno=EACCES;return -1;}if(!strcmp(mode,"remove")){errno=EROFS;return -1;}errno=ENOTTY;return 0;}
static int fake_rename(const char *from,const char *to)
{(void)from;(void)to;if(!strcmp(mode,"rename")){errno=EXDEV;return -1;}publishes++;return 0;}
#define fopen fake_open
#define fflush fake_flush
#define fclose fake_close
#define remove fake_remove
#define rename fake_rename
/* PRODUCTION_CITY_PAIR */
#undef fopen
#undef fflush
#undef fclose
#undef remove
#undef rename
int main(int argc,char **argv){
 if(argc!=2)return 2;mode=argv[1];Result result={0};Installer installer={&result,0};Recipe recipe={0};
 if(!strcmp(mode,"existing-detail"))strcpy(result.detail,"existing specific failure");
 recipe.op=!strncmp(mode,"cached",6)||!strcmp(mode,"success64")?64:63;recipe.arg[0]=2;
 if(!strcmp(mode,"context-max"))recipe.arg[0]=UINT_MAX;
 errno=0;int status=city_pair(&installer,&recipe,output),error=errno;
 printf("{\"return\":%d,\"status\":%d,\"errno\":%d,\"closes\":%u,\"flushes\":%u,\"cleanups\":%u,\"publishes\":%u,\"conversions\":%u,\"detail\":\"%s\"}\n",status,result.status,error,closes,flushes,cleanups,publishes,conversions,result.detail);return 0;
}
'''


class CityPairErrors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-city-pair-errors-')
        folder = Path(cls.temp.name)
        source = (ROOT / 'psp/src/portable_install.c').read_text()
        start = source.index('static int city_pair(')
        end = source.index('\nstatic int model_convert(', start)
        harness = folder / 'check.c'
        harness.write_text(HARNESS.replace('/* PRODUCTION_CITY_PAIR */', source[start:end]))
        cls.exe = folder / 'check'
        subprocess.run(['cc', '-std=gnu99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(ROOT / 'psp/src'),
                        str(harness), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, mode):
        proc = subprocess.run([self.exe, mode], capture_output=True, text=True, check=True)
        return json.loads(proc.stdout)

    def test_first_conversion_error_survives_successful_or_failed_cleanup(self):
        for mode, expected in [('convert', errno.ENOMEM), ('convert-and-cleanup', errno.ENOMEM),
                               ('convert-no-errno', 0)]:
            with self.subTest(mode=mode):
                result = self.run_case(mode)
                self.assertEqual((result['return'], result['errno']), (-1, expected))
                self.assertEqual((result['flushes'], result['closes'], result['cleanups']), (1, 1, 1))
                self.assertEqual(result['publishes'], 0)

    def test_flush_or_close_failure_is_not_replaced_by_later_cleanup(self):
        for mode, expected in [('flush', errno.ENOSPC), ('flush-and-close', errno.ENOSPC),
                               ('close', errno.EBADF)]:
            with self.subTest(mode=mode):
                result = self.run_case(mode)
                self.assertEqual((result['return'], result['errno']), (-1, expected))
                self.assertEqual((result['closes'], result['cleanups'], result['publishes']), (1, 1, 0))

    def test_sidecar_validation_and_publication_failures_preserve_first_error(self):
        for mode, expected in [('verify', errno.EILSEQ), ('remove', errno.EROFS), ('rename', errno.EXDEV)]:
            with self.subTest(mode=mode):
                result = self.run_case(mode)
                self.assertEqual((result['return'], result['errno']), (-1, expected))
                self.assertEqual(result['status'], -4 if mode == 'verify' else 0)
                self.assertEqual((result['cleanups'], result['publishes']), (1, 0))

    def test_cached_sidecar_copy_error_precedes_close_error(self):
        for mode, expected in [('cached-copy', errno.EIO), ('cached-close', errno.EBADF)]:
            with self.subTest(mode=mode):
                result = self.run_case(mode)
                self.assertEqual((result['return'], result['errno']), (-1, expected))
                self.assertEqual((result['conversions'], result['closes'], result['cleanups']), (0, 1, 0))

    def test_stage_failure_context_reaches_existing_installer_detail(self):
        result = self.run_case('context')
        self.assertEqual(result['errno'], errno.ENOMEM)
        self.assertEqual(result['detail'],
                         f'City stage=2 step=3 sec=6 errno={errno.ENOSPC} '
                         'src=123456 dst=120000 io=0/1 hf=6000000 kf=500000 km=200000')
        self.assertLess(len(result['detail']), 192)
        maximum = self.run_case('context-max')
        self.assertEqual(len(maximum['detail']), 189)
        self.assertTrue(maximum['detail'].endswith('hf=4294967295 kf=4294967295 km=4294967295'))
        self.assertEqual(maximum['errno'], errno.ENOMEM)
        self.assertEqual(self.run_case('stale-context')['detail'], '')
        self.assertEqual(self.run_case('existing-detail')['detail'], 'existing specific failure')

    def test_success_paths_keep_existing_publication_behavior(self):
        for mode, conversions, publishes, cleanups in [('success63', 1, 1, 0),
                                                     ('success64', 1, 0, 1),
                                                     ('cached-success', 0, 0, 0)]:
            with self.subTest(mode=mode):
                result = self.run_case(mode)
                self.assertEqual((result['return'], result['status']), (0, 0))
                self.assertEqual((result['conversions'], result['publishes'], result['cleanups']),
                                 (conversions, publishes, cleanups))


if __name__ == '__main__':
    unittest.main()
