"""Native attract dispatch, failure isolation, and coarse counter classification."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'psp/src'
DRIVER=r'''
#include <assert.h>
#include <stdint.h>
#include "portable_replay_session.c"
static unsigned generated,failed,tables,cache_failed;
static FILE *dependency(void *user,const char *name){(void)user;(void)name;assert(0);return NULL;}
int jgr_native_tables_load(JgrTableDependency d,void *u,char *e,size_t n)
{(void)d;(void)u;(void)e;(void)n;tables++;return 0;}
int jgr_native_math_load(JgrDisc *d){assert(d);return 1;}
void jgr_native_math_clear(void){}
JgrNativeCache *jgr_native_cache_create(JgrDisc *d,const char *s)
{assert(d&&s);return cache_failed?NULL:(JgrNativeCache *)(uintptr_t)1;}
void jgr_native_cache_destroy(JgrNativeCache *c){assert(!c||c==(JgrNativeCache *)(uintptr_t)1);}
int jgr_native_title_generate(const JgrNativeConfig *c,unsigned character,FILE *scratch,FILE *output)
{
 assert(c&&c->disc&&character<16&&scratch&&output);assert((c->cache==NULL)==cache_failed);generated++;
 if(failed){snprintf(c->error,c->error_size,"synthetic native title failure");return -1;}
 return fwrite("synthetic title",1,15,output)==15?0:-1;
}
int jgr_native_demo_generate(const JgrNativeConfig *c,const char *m,unsigned r,unsigned s,unsigned ch,FILE *a,FILE *b,unsigned char **q,unsigned *n)
{(void)c;(void)m;(void)r;(void)s;(void)ch;(void)a;(void)b;(void)q;(void)n;assert(0);return -1;}
int jgr_native_staff_record(const JgrNativeConfig *c,FILE *f){(void)c;(void)f;assert(0);return 0;}
int jgr_native_staff_generate(const JgrNativeConfig *c,unsigned ch,FILE *a,FILE *b,FILE *d,FILE *e)
{(void)c;(void)ch;(void)a;(void)b;(void)d;(void)e;assert(0);return 0;}
int jgr_native_rival_generate(const JgrNativeConfig *c,const char *m,unsigned r,unsigned s,unsigned ch,unsigned member,unsigned sections,const PortableReplayGangFixture *f,FILE *a,FILE *b)
{(void)c;(void)m;(void)r;(void)s;(void)ch;(void)member;(void)sections;(void)f;(void)a;(void)b;assert(0);return -1;}
int main(int argc,char **argv)
{
 assert(argc==2);ReplayJob unsupported={0};unsupported.demo=-1;unsupported.selection.kind=999;
 assert(!native_job_supported(&unsupported));
 for(unsigned i=0;i<JOBS;i++)assert(native_job_supported(replay_jobs+i));
 jgr_replay_stats_reset();
 for(unsigned attempt=0;attempt<4;attempt++){
  failed=attempt&1;cache_failed=attempt>>1;
  JgrReplaySession *s=jgr_replay_session_create((JgrDisc *)(uintptr_t)1,argv[1],dependency,NULL,NULL,NULL,NULL,0);
  assert(s&&!jgr_replay_session_native_assets(s,"synthetic-assets"));FILE *out=tmpfile();assert(out);
  assert(jgr_replay_session_generate(s,"unknown_route.bin",out)==-1 && !s->boot);
  int result=jgr_replay_session_generate(s,"title_route.bin",out);
  assert(!s->boot); /* A fake disc cannot run an interpreter: no fallback. */
  if(failed)assert(result==-1&&!strcmp(jgr_replay_session_error(s),"synthetic native title failure")&&ftell(out)==0);
  else{assert(result==0&&ftell(out)==15);rewind(out);char bytes[15];assert(fread(bytes,1,15,out)==15&&!memcmp(bytes,"synthetic title",15));}
  fclose(out);jgr_replay_session_destroy(s);
 }
 assert(generated==4&&tables==4);JgrReplayStats stats;jgr_replay_stats(&stats);
#if JGR_REPLAY_STATS
 assert(stats.native_jobs==4&&!stats.interpreted_jobs&&!stats.bootstrap_us);
 assert(stats.family_jobs[JGR_REPLAY_TITLE]==4&&stats.outputs==2&&stats.output_bytes==30);
#else
 assert(!stats.jobs&&!stats.native_jobs&&!stats.outputs);
#endif
 return 0;
}
'''
class NativeTitleSessionTests(unittest.TestCase):
    def test_native_title_dispatch_and_failure_never_allocate_interpreter(self):
        sources=sorted(p for p in SRC.glob('portable_replay*.c') if p.name!='portable_replay_session.c')
        sources += [SRC/(n+'.c') for n in ('portable_disc','portable_hardware','portable_tables','portable_convert')]
        with tempfile.TemporaryDirectory(prefix='jgr-native-title-session-') as temp:
            path=Path(temp);driver=path/'test.c';driver.write_text(DRIVER)
            for enabled in (0,1):
                with self.subTest(stats=enabled):
                    exe=path/('test'+str(enabled))
                    subprocess.run(['cc','-O2','-std=gnu99','-ffp-contract=off','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function','-Wno-unused-local-typedefs','-Wno-misleading-indentation',
                        '-fsanitize=address,undefined','-DJGR_NATIVE_REPLAY=1','-DJGR_REPLAY_STATS='+str(enabled),
                        '-I'+str(SRC),'-I'+str(ROOT/'psp/tests/render_stubs'),'-I'+str(ROOT/'psp/tests/stubs'),str(driver),*map(str,sources),'-lm','-o',str(exe)],check=True)
                    subprocess.run([str(exe),str(path)],check=True)
if __name__=='__main__':unittest.main()
