"""Synthetic async texture overlap, fallback, ownership and output ordering."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "portable_models.h"
#include "portable_setup_me.h"
#include "portable_texture_job.h"
JgrTextureJob *fixture_job_create(const unsigned char *,unsigned,unsigned,unsigned);
#define jgr_texture_job_create fixture_job_create
int fixture_read(JgrDisc *,const JgrDiscFile *,uint32_t,void *,size_t);
size_t fixture_available(void);
int fixture_next(const JgrJgcBuild *,unsigned);
#define jgr_disc_read fixture_read
#define jgr_scratch_heap_available fixture_available
#define jgr_jgc_next_texture fixture_next
#define JGR_SETUP_ME 1
#include "portable_models_jobs.c"
#undef jgr_disc_read
#undef jgr_scratch_heap_available
#undef jgr_jgc_next_texture
#undef jgr_texture_job_create
static pthread_t thread;
static JgrSetupMeWorker work;
static intptr_t argument;
static _Atomic int active,released,overlaps;
static int mode,joined,has_next=1,cancel_allowed;
static size_t heap=SIZE_MAX;
static unsigned char input[8192];
JgrTextureJob *fixture_job_create(const unsigned char *p,unsigned w,unsigned h,unsigned flags)
{return mode==7?NULL:jgr_texture_job_create(p,w,h,flags);}
static void *runner(void *unused){(void)unused;while(!released){};return (void*)(intptr_t)work(argument);}
int jgr_setup_me_available(void){return mode!=1;}
int jgr_setup_me_submit(JgrSetupMeWorker f,intptr_t arg,const JgrSetupMeRange *r,unsigned count){
 assert(count==1 && !(r->bytes&63) && !((uintptr_t)r->address&63) && r->flags==3 && !active);
 if(mode==2)return 0;
 work=f;argument=arg;released=!has_next;joined=0;active=1;assert(!pthread_create(&thread,NULL,runner,NULL));return 1;
}
int jgr_setup_me_wait(unsigned us,int *result){
 (void)us;assert(active);released=1;void *r;assert(!pthread_join(thread,&r));joined=1;
 if(mode==5)return JGR_SETUP_ME_PENDING;
 active=0;if(mode==3)return JGR_SETUP_ME_CANCELLED;
 *result=(int)(intptr_t)r;if(mode==4)*result=-1;
 if(mode==6){const unsigned char *p;unsigned flags;assert(!jgr_texture_job_result((JgrTextureJob*)argument,&p,&flags));((unsigned char*)p)[9]^=1;}
 return JGR_SETUP_ME_COMPLETE;
}
int jgr_setup_me_cancel(void){if(!active)return 1;if(!cancel_allowed)return 0;assert(joined);active=0;return 1;}
int fixture_next(const JgrJgcBuild *b,unsigned start){(void)b;return has_next && start==1?1:-1;}
size_t fixture_available(void){return heap;}
int fixture_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){
 (void)d;if(at>f->size || n>f->size-at)return -1;
 if(active){overlaps++;released=1;}
 memcpy(out,input+at,n);return 0;
}
static void put32(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static void setup(Textures *t,TextureBank *b,uint32_t offsets[2],uint16_t caps[2],unsigned char used[2]){
 memset(input,0,sizeof input);memset(t,0,sizeof *t);memset(b,0,sizeof *b);
 for(unsigned k=0;k<2;k++){unsigned char *p=input+k*4096;memcpy(p,"GBIX",4);memcpy(p+8,"PVRT",4);put32(p+12,8+32*32*2);p[16]=2;p[17]=9;p[20]=32;p[22]=32;
  for(unsigned i=0;i<32*32;i++){unsigned v=(i*1777u+k*921u)&65535;p[24+2*i]=v;p[25+2*i]=v>>8;}offsets[k]=k*4096;caps[k]=16;used[k]=0;}
 t->disc=(JgrDisc*)input;t->banks=b;t->count=1;t->total=2;t->caps=caps;t->used_caps=used;t->build=(JgrJgcBuild*)t;
 b->source.size=sizeof input;b->count=2;b->offsets=offsets;b->cap=32;
 texture_worker_verified=texture_worker_disabled=0;overlaps=0;assert(!texture_quarantine&&!active);
}
static unsigned render(Textures *t,int cpu,unsigned char *output,JgrJgcTexture rec[2]){
 FILE *f=tmpfile();assert(f);for(unsigned i=0;i<7;i++)assert(fputc(0x71,f)!=EOF);
 for(unsigned i=0;i<2;i++)assert(!(cpu?emit_texture_cpu(i,f,rec+i,t):emit_texture(i,f,rec+i,t)));
 unsigned n=(unsigned)ftell(f);rewind(f);assert(fread(output,1,n,f)==n);fclose(f);return n;
}
int main(void){
 for(mode=0;mode<=7;mode++){
  if(mode==5)continue;
  Textures t;TextureBank b;uint32_t offsets[2];uint16_t caps[2];unsigned char used[2],actual[8192],expected[8192];JgrJgcTexture ar[2],er[2];
  setup(&t,&b,offsets,caps,used);unsigned n=render(&t,1,expected,er);unsigned count=render(&t,0,actual,ar);
  assert(n==count&&!memcmp(actual,expected,n)&&!memcmp(ar,er,sizeof ar));assert(!active&&!t.prepared);
  if(mode==0 || mode==3 || mode==4 || mode==6)assert(overlaps>0);
  if(mode==6)assert(texture_worker_disabled);
 }
 /* A constrained allocator never starts work and preserves CPU output. */
 mode=0;heap=6u*1024u*1024u;Textures t;TextureBank b;uint32_t offsets[2];uint16_t caps[2];unsigned char used[2],a[8192],e[8192];JgrJgcTexture ar[2],er[2];
 setup(&t,&b,offsets,caps,used);unsigned n=render(&t,1,e,er);assert(render(&t,0,a,ar)==n&&!memcmp(a,e,n)&&!overlaps);heap=SIZE_MAX;

 /* Encoded content hits bypass ME work, including prefetched cache hits.
    Different source offsets with identical bytes retain exact output order. */
 setup(&t,&b,offsets,caps,used);memcpy(input+4096,input,4096);mode=0;
 n=render(&t,1,e,er);char directory[]="/tmp/jgr-me-cache-XXXXXX";assert(mkdtemp(directory));
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(texture_disk.file);
 assert(render(&t,0,a,ar)==n&&!memcmp(a,e,n));assert(texture_disk.count>0&&!active);
 /* A different disc can use the converter while this scope exists, but its
    ME results must not be appended to the original disc's content index. */
 jgr_models_cache_clear();JgrDisc *owner=t.disc;unsigned entries=texture_disk.count,bytes=texture_disk.bytes;
 t.disc=(JgrDisc*)&b;overlaps=0;
 assert(render(&t,0,a,ar)==n&&!memcmp(a,e,n)&&!memcmp(ar,er,sizeof ar));
 assert(overlaps>0&&!active&&!t.prepared&&texture_disk.count==entries&&texture_disk.bytes==bytes);
 t.disc=owner;
 jgr_models_cache_clear();overlaps=0;
 assert(render(&t,0,a,ar)==n&&!memcmp(a,e,n)&&!memcmp(ar,er,sizeof ar)&&!overlaps&&!active&&!t.prepared);
 jgr_models_cache_end();assert(!rmdir(directory));
 /* Unfenced ownership aborts conversion without touching borrowed memory. */
 setup(&t,&b,offsets,caps,used);mode=5;FILE *f=tmpfile();assert(f&&emit_texture(0,f,ar,&t));assert(texture_quarantine&&active);
 assert(emit_texture(1,f,ar,&t));assert(texture_quarantine&&active);cancel_allowed=1;assert(jgr_setup_me_cancel());
 jgr_texture_job_destroy(texture_quarantine);texture_quarantine=NULL;prepared_free(t.prepared);t.prepared=NULL;fclose(f);
 return 0;
}
'''
class TexturePipelineTests(unittest.TestCase):
    def test_async_overlap_fallback_and_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/'test.c').write_text(DRIVER)
            units=['portable_indexed.c','portable_texture_job.c','portable_convert.c']
            subprocess.run(['cc','-std=gnu11','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'psp/src'),str(d/'test.c'),*[str(ROOT/'psp/src'/n) for n in units],'-Wl,-dead_strip','-pthread','-lm','-o',str(d/'test')],check=True)
            subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':unittest.main()
