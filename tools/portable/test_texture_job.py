"""Payload-free parity and allocator tests for the pure whole-texture worker."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "portable_texture_job.c"
static unsigned seed=7;
static unsigned random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static void check(unsigned sw,unsigned sh,unsigned w,unsigned h,unsigned pattern){
 size_t n=(size_t)sw*sh*4,bytes=1024+(size_t)w*h;
 unsigned char *src=malloc(n),*copy=malloc(n),*expected=malloc(bytes);assert(src&&copy&&expected);
 for(size_t i=0;i<n;i++)src[i]=(unsigned char)(pattern==0?random_word()>>24:pattern==1?(i%4==3?255:0):pattern==2?0:((i/4)%256));
 if(pattern==4)for(size_t i=3;i<n;i+=4)src[i]=((i/4)&1)?0:255;
 memcpy(copy,src,n);unsigned flags=99,prepared_flags=99;
 assert(!jgr_indexed_memory(copy,sw,sh,w,h,expected,bytes,&flags));
 unsigned char *prepared=jgr_indexed_prepare(src,sw,sh,w,h,&prepared_flags);assert(prepared&&flags==prepared_flags);
 unsigned char *immutable=malloc((size_t)w*h*4);assert(immutable);memcpy(immutable,prepared,(size_t)w*h*4);
 JgrTextureJob *job=jgr_texture_job_create(prepared,w,h,flags);assert(job);
 size_t range=0;assert(jgr_texture_job_range(job,&range)==job && !(range&63) && !((uintptr_t)job&63));
 assert(range==jgr_texture_job_bytes(w,h));
 const unsigned char *out=NULL;unsigned actual=99;assert(jgr_texture_job_result(job,&out,&actual));
 assert(!jgr_texture_job_worker((intptr_t)job));assert(!jgr_texture_job_result(job,&out,&actual));
 assert(flags==actual&&!memcmp(out,expected,bytes)&&!memcmp(prepared,immutable,(size_t)w*h*4));
 assert(!memcmp((unsigned char*)job+job->input_at,immutable,(size_t)w*h*4));
 assert(!jgr_texture_job_verify(job));
 /* A bad worker output is detected; fenced CPU retry restores exact bytes. */
 ((unsigned char*)out)[17]^=1;assert(jgr_texture_job_verify(job));assert(!jgr_texture_job_retry(job));assert(!memcmp(out,expected,bytes));
 job->status=-1;assert(jgr_texture_job_result(job,&out,&actual));assert(!jgr_texture_job_retry(job));
 uint32_t saved=job->arena_bytes;job->arena_bytes=16;assert(jgr_texture_job_worker((intptr_t)job));job->arena_bytes=saved;
 assert(!jgr_texture_job_worker((intptr_t)job));assert(!jgr_texture_job_verify(job));
 jgr_texture_job_destroy(job);free(immutable);free(prepared);free(expected);free(copy);free(src);
}
int main(void){
 assert(!jgr_texture_job_bytes(0,8)&&!jgr_texture_job_bytes(15,8)&&!jgr_texture_job_bytes(16,7)&&!jgr_texture_job_bytes(2048,8));
 assert(!jgr_texture_job_create(NULL,16,8,0));assert(jgr_texture_job_worker(0));
 for(unsigned p=0;p<5;p++){check(32,32,16,16,p);check(16,8,16,8,p);check(128,128,128,128,p);}
 check(256,128,128,64,0);check(16,8,32,16,4);check(512,512,512,512,0);check(1024,1024,1024,1024,0);
 /* Arena exhaustion and fragmented free ranges are bounded and recoverable. */
 unsigned char data[512] __attribute__((aligned(64)));TextureArena arena={data,sizeof data};worker_arena=&arena;
 ArenaBlock *b=(ArenaBlock*)data;b->bytes=sizeof data-sizeof *b;b->used=0;
 void *a=texture_alloc(32),*c=texture_alloc(64),*d=texture_alloc(32);assert(a&&c&&d&&!texture_alloc(1024));
 texture_free(a);texture_free(c);assert(texture_alloc(96)==a);texture_free(d);assert(!texture_calloc(SIZE_MAX,2));worker_arena=NULL;
 return 0;
}
'''
class TextureJobTests(unittest.TestCase):
    def test_pure_kernel_parity_and_bounded_arena(self):
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/'test.c').write_text(DRIVER)
            subprocess.run(['cc','-std=gnu99','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'psp/src'),str(d/'test.c'),str(ROOT/'psp/src/portable_indexed.c'),'-lm','-o',str(d/'test')],check=True)
            subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':unittest.main()
