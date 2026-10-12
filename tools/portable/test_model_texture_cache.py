"""Synthetic, payload-free coverage for optional setup texture reuse."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
static unsigned failed_size,memory_calls,file_calls,fail_memory;
static size_t free_bytes=SIZE_MAX,fail_calloc_bytes;
static unsigned calloc_failures,fail_disk_write,fail_disk_seek,fail_disk_close,offset_reads,use_real_disc;
static FILE *partial_output,*partial_lighting;
static long partial_output_bytes,partial_lighting_bytes;
static unsigned partial_failures;
static void *cache_calloc(size_t n,size_t size){if(n*size==fail_calloc_bytes){fail_calloc_bytes=0;calloc_failures++;errno=ENOMEM;return NULL;}return calloc(n,size);}
static size_t cache_write(const void *,size_t,size_t,FILE *);
static int cache_seek(FILE *,long,int);
static int cache_close(FILE *);
static void *cache_malloc(size_t n){return n==failed_size?NULL:malloc(n);}
#define malloc cache_malloc
#define calloc cache_calloc
#define fwrite cache_write
#define fseek cache_seek
#define fclose cache_close
#define jgr_disc_read fixture_read
#define jgr_scratch_heap_available fixture_available
#define jgr_indexed_memory counted_memory
#define jgr_indexed_texture counted_file
#include "portable_models_jobs.c"
#undef malloc
#undef calloc
#undef fwrite
#undef fseek
#undef fclose
#undef jgr_disc_read
#undef jgr_scratch_heap_available
#undef jgr_indexed_memory
#undef jgr_indexed_texture
extern int jgr_indexed_memory(unsigned char *,unsigned,unsigned,unsigned,unsigned,unsigned char *,size_t,unsigned *);
extern int jgr_indexed_texture(unsigned char *,unsigned,unsigned,unsigned,unsigned,FILE *,JgrJgcTexture *);
extern int jgr_disc_read(JgrDisc *,const JgrDiscFile *,uint32_t,void *,size_t);
static size_t cache_write(const void *p,size_t n,size_t count,FILE *file){
 if(partial_output==file&&!partial_failures&&ftell(file)>0){
  partial_output_bytes=ftell(file);partial_lighting_bytes=partial_lighting?ftell(partial_lighting):0;
  partial_failures++;errno=ENOMEM;return 0;
 }
 if(fail_disk_write && file==texture_disk.file){fail_disk_write=0;errno=ENOSPC;return 0;}
 return fwrite(p,n,count,file);
}
static int cache_seek(FILE *file,long at,int whence){
 if(fail_disk_seek&&file==texture_disk.file){fail_disk_seek=0;errno=ENOSPC;return -1;}
 return fseek(file,at,whence);
}
static int cache_close(FILE *file){
 int fail=fail_disk_close&&file==texture_disk.file;if(fail)fail_disk_close=0;
 int result=fclose(file);if(fail){errno=ENOSPC;return EOF;}return result;
}
typedef struct {unsigned char data[8192];unsigned reads;} Input;
int fixture_read(JgrDisc *d,const JgrDiscFile *f,uint32_t at,void *out,size_t n){
 if(use_real_disc)return jgr_disc_read(d,f,at,out,n);
 Input *in=(Input*)d;if(at>f->size || n>f->size-at)return -1;
 uint64_t start=at+(offset_reads?f->offset:0);if(start>sizeof in->data || n>sizeof in->data-start)return -1;
 memcpy(out,in->data+start,n);in->reads++;return 0;
}
size_t fixture_available(void){return free_bytes;}
int counted_memory(unsigned char *p,unsigned sw,unsigned sh,unsigned w,unsigned h,unsigned char *out,size_t n,unsigned *flags){
 memory_calls++;if(fail_memory){fail_memory=0;p[0]^=255;return -1;}return jgr_indexed_memory(p,sw,sh,w,h,out,n,flags);
}
int counted_file(unsigned char *p,unsigned sw,unsigned sh,unsigned w,unsigned h,FILE *f,JgrJgcTexture *r){
 file_calls++;return jgr_indexed_texture(p,sw,sh,w,h,f,r);
}
static void put32(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static void fixture(Input *in,Textures *t,TextureBank *bank,uint32_t *offset,uint16_t *cap,unsigned char *used){
 memset(in,0,sizeof *in);memcpy(in->data,"GBIX",4);memcpy(in->data+8,"PVRT",4);
 put32(in->data+12,8+32*32*2);in->data[16]=2;in->data[17]=9;in->data[20]=32;in->data[22]=32;
 for(unsigned i=0;i<32*32;i++){unsigned v=(i*1777u)&65535;in->data[24+i*2]=v;in->data[25+i*2]=v>>8;}
 memset(t,0,sizeof *t);memset(bank,0,sizeof *bank);*offset=0;*cap=16;*used=0;
 t->disc=(JgrDisc*)in;t->banks=bank;t->count=1;t->total=1;t->caps=cap;t->used_caps=used;
 bank->source.size=sizeof in->data;bank->count=1;bank->offsets=offset;bank->cap=32;
}
static unsigned render(Textures *t,unsigned prefix,unsigned char *out,JgrJgcTexture *r){
 FILE *f=tmpfile();assert(f);for(unsigned i=0;i<prefix;i++)assert(fputc(0x6d,f)!=EOF);
 assert(!emit_texture(0,f,r,t));long n=ftell(f);assert(n>0 && n<8192);rewind(f);assert(fread(out,1,n,f)==(size_t)n);assert(!fclose(f));return (unsigned)n;
}
static void lifecycle(void){
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,a[8192],z[8192];JgrJgcTexture ar,zr;
 fixture(&in,&t,&b,&offset,&cap,&used);unsigned expected=render(&t,13,a,&ar);assert(file_calls==1 && !memory_calls);
 jgr_models_cache_begin(t.disc);unsigned actual=render(&t,13,z,&zr);assert(expected==actual && !memcmp(a,z,actual) && !memcmp(&ar,&zr,sizeof ar));
 assert(memory_calls==1 && texture_cache.bytes==1280 && used==1);used=0;unsigned reads=in.reads;
 actual=render(&t,13,z,&zr);assert(expected==actual && !memcmp(a,z,actual) && memory_calls==1 && in.reads==reads+1 && used==1);
 /* Destination offsets/padding belong to this stream, never the cached one. */
 actual=render(&t,47,z,&zr);assert(zr.clut==48 && zr.pixels==1072 && actual==48+1280);
 assert(!memcmp(a+ar.clut,z+zr.clut,1280) && memory_calls==1);
 jgr_models_cache_clear();assert(!texture_cache.bytes && texture_cache.disc==t.disc);render(&t,0,z,&zr);assert(memory_calls==2);
 jgr_models_cache_end();assert(!texture_cache.bytes && !texture_cache.disc);render(&t,0,z,&zr);assert(file_calls==2);
 jgr_models_cache_begin(t.disc);render(&t,0,z,&zr);jgr_models_cache_begin(t.disc);assert(!texture_cache.bytes);render(&t,0,z,&zr);assert(memory_calls==4);
 Input other=in;t.disc=(JgrDisc*)&other;render(&t,0,z,&zr);assert(file_calls==3);jgr_models_cache_end();
 JgrModelsStats stats;jgr_models_stats(NULL);jgr_models_stats(&stats);
 assert(stats.textures==9 && stats.cache_hits==2 && !stats.jobs && !stats.rows);
 assert(stats.texture_us>=stats.cpu_texture_us && !stats.prepare_us && !stats.wait_us);
}
static void key_options(void){
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,a[8192],z[8192];JgrJgcTexture ar,zr;
 fixture(&in,&t,&b,&offset,&cap,&used);jgr_models_cache_begin(t.disc);render(&t,0,z,&zr);
 for(unsigned variant=0;variant<8;variant++){
  fixture(&in,&t,&b,&offset,&cap,&used);
  if(variant==0)t.white=1;
  if(variant==1)cap=32;
  if(variant==2)b.source.offset=4096;
  if(variant==3)b.source.size--;
  if(variant==4)in.data[16]=1;
  if(variant==5)in.data[17]=0xb;
  if(variant==6){in.data[20]=16;put32(in.data+12,8+16*32*2);}
  if(variant==7){put32(in.data+12,8+32*32*2+16);}
  unsigned calls=memory_calls,n=render(&t,3,z,&zr);assert(memory_calls==calls+1);
  JgrDisc *saved=texture_cache.disc;texture_cache.disc=NULL;
  unsigned expected=render(&t,3,a,&ar);texture_cache.disc=saved;
  assert(n==expected && !memcmp(a,z,n) && !memcmp(&ar,&zr,sizeof ar));
  calls=memory_calls;render(&t,3,z,&zr);assert(memory_calls==calls);
 }
 /* A hit must propagate destination failures rather than report success. */
 FILE *bad=fopen("/dev/null","rb");assert(bad);setvbuf(bad,NULL,_IONBF,0);
 assert(emit_texture(0,bad,&zr,&t));fclose(bad);
 /* The read header is still validated before consulting a cached result. */
 in.data[20]=0;FILE *f=tmpfile();assert(f && emit_texture(0,f,&zr,&t));fclose(f);jgr_models_cache_end();
}
static void limits(void){
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,out[8192];JgrJgcTexture r;
 fixture(&in,&t,&b,&offset,&cap,&used);jgr_models_cache_begin(t.disc);render(&t,0,out,&r);
 TextureKey first=texture_cache.entries[0].key;
 for(unsigned i=1;i<65;i++){b.source.offset=i*4096;render(&t,0,out,&r);}
 assert(!texture_cache_find(t.disc,&first) && texture_cache.bytes==64*1280);
 jgr_models_cache_clear();
 for(unsigned i=0;i<10;i++){
  TextureKey key;memset(&key,0,sizeof key);key.source_offset=i;unsigned bytes=1024+128*128;
  unsigned char *p=texture_cache_allocate(t.disc,bytes);assert(p);texture_cache_store(&key,p,bytes,0);
 }
 /* Large entries evict by byte budget independently of entry count. */
 for(unsigned i=10;i<30;i++){
  TextureKey key;memset(&key,0,sizeof key);key.source_offset=i;unsigned bytes=1024+256*256;
  unsigned char *p=texture_cache_allocate(t.disc,bytes);assert(p);texture_cache_store(&key,p,bytes,0);
  assert(texture_cache.bytes<=TEXTURE_CACHE_BYTES);
 }
 free_bytes=TEXTURE_CACHE_HEADROOM-1;texture_cache_trim();assert(!texture_cache.bytes);
 unsigned calls=file_calls;render(&t,0,out,&r);assert(file_calls==calls+1 && !texture_cache.bytes);
 free_bytes=SIZE_MAX;jgr_models_cache_end();
}
static void allocation_failure(void){
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,a[8192],z[8192];JgrJgcTexture ar,zr;
 fixture(&in,&t,&b,&offset,&cap,&used);unsigned n=render(&t,0,a,&ar);
 jgr_models_cache_begin(t.disc);failed_size=1280;assert(render(&t,0,z,&zr)==n && !memcmp(a,z,n));assert(!texture_cache.bytes && !memory_calls);
 failed_size=0;fail_memory=1;assert(render(&t,0,z,&zr)==n && !memcmp(a,z,n));assert(!texture_cache.bytes && memory_calls==1);
 render(&t,0,z,&zr);assert(texture_cache.bytes==1280 && memory_calls==2);jgr_models_cache_end();
}

static void disk_reuse(void){
 char directory[]="/tmp/jgr-disk-texture-XXXXXX";assert(mkdtemp(directory));
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,a[8192],z[8192];JgrJgcTexture ar,zr;
 fixture(&in,&t,&b,&offset,&cap,&used);offset_reads=1;b.source.size=4096;memcpy(in.data+4096,in.data,4096);
 unsigned n=render(&t,7,a,&ar);jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(texture_disk.file);
 unsigned calls=memory_calls;assert(render(&t,7,z,&zr)==n&&!memcmp(a,z,n));assert(memory_calls==calls+1 && texture_disk.count==1);
 jgr_models_cache_clear();assert(texture_disk.file&&texture_disk.count==1);b.source.offset=4096;
 calls=memory_calls;assert(render(&t,7,z,&zr)==n&&!memcmp(a,z,n));assert(memory_calls==calls);
 /* Larger containers and caps which produce the same dimensions do not
    change the content identity; output offsets are still relocated. */
 b.source.offset=0;b.source.size=8192;calls=memory_calls;
 assert(render(&t,47,z,&zr)==48+1280&&!memcmp(a+ar.clut,z+zr.clut,1280));assert(memory_calls==calls);
 FILE *bad=fopen("/dev/null","rb");assert(bad);setvbuf(bad,NULL,_IONBF,0);
 assert(emit_texture(0,bad,&zr,&t));fclose(bad);
 char path[1024];strcpy(path,texture_disk.path);jgr_models_cache_end();assert(access(path,F_OK));
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(texture_disk.file&&!texture_disk.count);
 render(&t,0,z,&zr);assert(memory_calls==calls+1);jgr_models_cache_end();assert(!rmdir(directory));
}
static void disk_collisions_and_corruption(void){
 char directory[]="/tmp/jgr-disk-texture-XXXXXX";assert(mkdtemp(directory));
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,z[8192];JgrJgcTexture zr;
 fixture(&in,&t,&b,&offset,&cap,&used);offset_reads=1;b.source.size=4096;memcpy(in.data+4096,in.data,4096);
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);render(&t,0,z,&zr);
 assert(texture_disk.count==1);DiskTexture original=texture_disk.entries[0];TextureKey other=original.key;other.source_offset=4096;
 unsigned flags=99;uint32_t crc=original.raw_crc;in.data[4096+24]^=1;
 /* Deliberately present different payload bytes with the SAME lookup CRC.
    Exact source comparison must reject the candidate, not trust the hash. */
 assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags));assert(texture_disk.file);
 in.data[4096+24]^=1;unsigned char *hit=texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags);
 assert(hit&&!memcmp(hit,z,1280));free(hit);
 other.white=1;assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags));other.white=0;
 other.format^=1;assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags));other.format^=1;
 other.width*=2;assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags));other.width/=2;
 /* Corrupting the private encoded file invalidates the optional cache and
    forces exact regeneration rather than publishing stale pixels. */
 assert(!fflush(texture_disk.file));assert(!fseek(texture_disk.file,17,SEEK_SET));assert(fputc(z[17]^1,texture_disk.file)!=EOF);assert(!fflush(texture_disk.file));
 char path[1024];strcpy(path,texture_disk.path);assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags));assert(!texture_disk.file&&access(path,F_OK));
 jgr_models_cache_clear();unsigned calls=memory_calls;b.source.offset=4096;render(&t,0,z,&zr);assert(memory_calls==calls+1);
 jgr_models_cache_end();
 /* A truncated encoded payload also becomes a cache miss. */
 b.source.offset=0;jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);render(&t,0,z,&zr);
 assert(texture_disk.count==1&&!fflush(texture_disk.file));assert(!ftruncate(fileno(texture_disk.file),17));
 assert(!texture_disk_find(t.disc,&other,in.data+4096+24,crc,&flags)&&!texture_disk.file);
 jgr_models_cache_end();assert(!rmdir(directory));
}
static void disk_failures_and_limits(void){
 char directory[]="/tmp/jgr-disk-texture-XXXXXX";assert(mkdtemp(directory));char path[1024],sentinel[1024];
 snprintf(path,sizeof path,"%s/%s",directory,JGR_MODEL_TEXTURE_CACHE_FILE);snprintf(sentinel,sizeof sentinel,"%s/sentinel",directory);
 Input in;Textures t;TextureBank b;uint32_t offset;uint16_t cap;unsigned char used,z[8192];JgrJgcTexture zr;
 fixture(&in,&t,&b,&offset,&cap,&used);
 assert(!mkdir(path,0700));jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(!texture_disk.file);jgr_models_cache_end();assert(!rmdir(path));
 FILE *f=fopen(sentinel,"wb");assert(f&&fwrite("keep",1,4,f)==4&&!fclose(f));assert(!symlink(sentinel,path));
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(!texture_disk.file);jgr_models_cache_end();assert(!unlink(path));
 f=fopen(sentinel,"rb");char text[4];assert(f&&fread(text,1,4,f)==4&&!memcmp(text,"keep",4)&&!fclose(f));assert(!unlink(sentinel));
 failed_size=DISK_TEXTURE_BUFFER;jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(!texture_disk.file&&!texture_disk.entries&&!texture_disk.slots);failed_size=0;jgr_models_cache_end();
 free_bytes=TEXTURE_CACHE_HEADROOM;jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(!texture_disk.file);free_bytes=SIZE_MAX;jgr_models_cache_end();
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);assert(texture_disk.file);fail_disk_write=1;
 render(&t,0,z,&zr);assert(!fail_disk_write&&!texture_disk.file&&access(path,F_OK));jgr_models_cache_end();
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);render(&t,0,z,&zr);assert(texture_disk.count==1);
 DiskTexture entry=texture_disk.entries[0];texture_disk.bytes=JGR_MODEL_TEXTURE_CACHE_DISK_BYTES-1;
 texture_disk_store(t.disc,&entry.key,entry.raw_crc,z,entry.flags);assert(texture_disk.count==1);
 texture_disk.bytes=entry.bytes;texture_disk.count=DISK_TEXTURE_ENTRIES;
 texture_disk_store(t.disc,&entry.key,entry.raw_crc,z,entry.flags);assert(texture_disk.bytes==entry.bytes);
 jgr_models_cache_end();
 /* Buffered writes can fail only when the next seek flushes them. This is
    optional state, so failure must discard the cache and permit regeneration. */
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);render(&t,0,z,&zr);
 entry=texture_disk.entries[0];fail_disk_seek=1;unsigned flags;
 assert(!texture_disk_find(t.disc,&entry.key,in.data+24,entry.raw_crc,&flags));
 assert(!fail_disk_seek&&!texture_disk.file&&access(path,F_OK));jgr_models_cache_end();
 jgr_models_cache_begin(t.disc);texture_disk_open(t.disc,directory);render(&t,0,z,&zr);fail_disk_close=1;
 jgr_models_cache_end();assert(!fail_disk_close&&!texture_disk.file&&access(path,F_OK));assert(!rmdir(directory));
}
static void disk_retry(const char *gdi,const char *recipe,const char *directory,int partial){
 use_real_disc=1;JgrDisc *disc=NULL;char error[192];assert(!jgr_disc_open(&disc,gdi,error,sizeof error));
 FILE *plan=fopen(recipe,"rb"),*a=tmpfile(),*b=tmpfile();assert(plan&&a&&b);assert(!fseek(plan,0,SEEK_END));long size=ftell(plan);assert(size>0);
 FILE *la=partial?tmpfile():NULL,*lb=partial?tmpfile():NULL;
 assert(!partial || (la&&lb));
 assert(!jgr_models_convert_with_lighting(disc,plan,0,(uint32_t)size,directory,a,la));
 jgr_models_cache_begin(disc);texture_disk_open(disc,directory);assert(texture_disk.file);
 if(partial){partial_output=b;partial_lighting=lb;}else fail_calloc_bytes=MAX_BANKS*sizeof(TextureBank);
 assert(!jgr_models_convert_with_lighting(disc,plan,0,(uint32_t)size,directory,b,lb));
 assert(!texture_disk.file&&!texture_cache.disc);
 if(partial)assert(partial_failures==1&&partial_output_bytes>0&&partial_lighting_bytes>0);
 else assert(calloc_failures==1);
 assert(ftell(a)==ftell(b));rewind(a);rewind(b);unsigned char x[8192],y[8192];size_t n;
 while((n=fread(x,1,sizeof x,a))){assert(fread(y,1,n,b)==n&&!memcmp(x,y,n));}
 if(partial){assert(ftell(la)==ftell(lb));rewind(la);rewind(lb);
  while((n=fread(x,1,sizeof x,la))){assert(fread(y,1,n,lb)==n&&!memcmp(x,y,n));}
  fclose(la);fclose(lb);partial_output=partial_lighting=NULL;
 }
 fclose(a);fclose(b);fclose(plan);jgr_models_cache_end();jgr_disc_close(disc);
}
int main(int argc,char **argv){if(argc==5){disk_retry(argv[1],argv[2],argv[3],!strcmp(argv[4],"partial"));return 0;}assert(argc==2);if(!strcmp(argv[1],"lifecycle"))lifecycle();
 else if(!strcmp(argv[1],"options"))key_options();else if(!strcmp(argv[1],"limits"))limits();
 else if(!strcmp(argv[1],"allocation"))allocation_failure();
 else if(!strcmp(argv[1],"disk"))disk_reuse();else if(!strcmp(argv[1],"collision"))disk_collisions_and_corruption();
 else if(!strcmp(argv[1],"diskfail"))disk_failures_and_limits();else return 2;return 0;}
'''


class ModelTextureCache(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('Host compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory(prefix='jgr-texture-cache-')
        work = Path(cls.tmp.name)
        source = work/'cache.c'
        source.write_text(DRIVER)
        cls.driver = work/'cache'
        units = ['portable_models_colors', 'portable_models_pack', 'portable_models',
                 'portable_disc', 'portable_convert', 'portable_indexed', 'portable_scratch']
        subprocess.run(['cc', '-std=gnu99', '-O1', '-g', '-ffp-contract=off',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-Wall', '-Wextra', '-Werror', '-DJGR_MODELS_STATS=1', '-I'+str(ROOT/'psp/src'), str(source),
                        *[str(ROOT/'psp/src'/f'{u}.c') for u in units], '-lm', '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_hits_relocate_records_and_session_cleanup(self):
        subprocess.run([self.driver, 'lifecycle'], check=True)

    def test_source_and_conversion_options_do_not_alias(self):
        subprocess.run([self.driver, 'options'], check=True)

    def test_bounded_lru_and_low_heap_fallback(self):
        subprocess.run([self.driver, 'limits'], check=True)

    def test_optional_allocation_and_encoder_failure_fallback(self):
        subprocess.run([self.driver, 'allocation'], check=True)

    def test_disk_reuse_across_source_locations_and_scope_cleanup(self):
        subprocess.run([self.driver, 'disk'], check=True)

    def test_disk_crc_collision_and_encoded_corruption_are_misses(self):
        subprocess.run([self.driver, 'collision'], check=True)

    def test_disk_paths_io_oom_and_limits_keep_ordinary_conversion(self):
        subprocess.run([self.driver, 'diskfail'], check=True)

    def test_admitted_cache_cannot_cause_mandatory_model_allocation_failure(self):
        supplied=os.environ.get('JGR_TEST_GDI')
        if not supplied:self.skipTest('Set JGR_TEST_GDI to verify full model retry against your disc')
        from model_recipes import recipes
        with tempfile.TemporaryDirectory(prefix='jgr-model-cache-retry-') as directory:
            plan=Path(directory)/'model.recipe';plan.write_bytes(recipes()['bantam_train.jgc']['block'])
            subprocess.run([self.driver,supplied,plan,directory,'retry'],check=True)
            plan.write_bytes(recipes()['chars.jgc']['block'])
            subprocess.run([self.driver,supplied,plan,directory,'partial'],check=True)


if __name__ == '__main__':
    unittest.main()
